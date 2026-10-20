#include <QCoreApplication>
#include <anvil/translation.h>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QLockFile>
#include <memory>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
#endif
namespace anvil {
namespace {
[[noreturn]] void fail(const QString &s) { throw std::runtime_error(s.toStdString()); }
QString tokensPattern() {
    return R"(\{[^{}\r\n]*\}|\[[A-Z_][A-Z0-9_:. -]*\]|</?[^<>\r\n]+>|%(?:\d+\$)?[-+#0 ]*\d*(?:\.\d+)?[hlL]*[sdifuxXocgeEGp]|%%|\\[nrt]|\r\n|\n|\r)";
}
struct ServerProcess {
    QProcess process;
    ~ServerProcess() {
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(1000)) { process.kill(); process.waitForFinished(2000); }
        }
    }
};
}
QVector<TranslationGroup> groupTranslationEntries(const QVector<Entry> &entries) {
    QHash<QString,int> seen;
    QVector<TranslationGroup> groups;
    for (qsizetype i=0;i<entries.size();++i) {
        const auto found=seen.constFind(entries[i].source);
        if (found==seen.cend()) {
            seen.insert(entries[i].source,groups.size());
            groups.append({entries[i].source,{int(i)}});
        } else groups[*found].rows.append(i);
    }
    return groups;
}
bool isServiceTranslationText(const QString &source) {
    static const QRegularExpression re(R"(^\s*(?:(?:Name|Context|Acting)\s*:|//WARNING))");
    return re.match(source).hasMatch();
}
ProtectedText protectTranslation(const QString &source, const QVector<Term> &glossary) {
    if (source.contains("__ANVIL_") || source.size() > 6000 || source.trimmed().isEmpty())
        fail(QCoreApplication::translate("Anvil", "Рядок порожній, завеликий (понад 6000 символів) або містить службову позначку"));
    ProtectedText plan;
    const QRegularExpression tokens(tokensPattern());
    auto matches = tokens.globalMatch(source);
    qsizetype cursor = 0;
    auto mask = [&](const QString &replacement) {
        const auto token = QString("__ANVIL_%1__").arg(plan.replacements.size(), 4, 10, QChar('0'));
        plan.replacements.append({token, replacement});
        return token;
    };
    auto protectPlain = [&](const QString &plain) {
        if (glossary.isEmpty()) return plain;
        QVector<Term> ordered = glossary;
        std::sort(ordered.begin(), ordered.end(), [](const Term &a, const Term &b) {
            return a.source.size() > b.source.size();
        });
        QStringList alternatives;
        for (const auto &term : ordered)
            if (!term.source.isEmpty()) alternatives << QRegularExpression::escape(term.source);
        if (alternatives.isEmpty()) return plain;
        const QRegularExpression re("(?<![\\p{L}\\p{N}_])(?:" + alternatives.join('|') +
                                    ")(?![\\p{L}\\p{N}_])", QRegularExpression::CaseInsensitiveOption);
        QString result;
        auto terms = re.globalMatch(plain);
        qsizetype at = 0;
        while (terms.hasNext()) {
            const auto match = terms.next();
            result += plain.mid(at, match.capturedStart() - at);
            for (const auto &term : ordered)
                if (term.source.compare(match.captured(), Qt::CaseInsensitive) == 0) {
                    result += mask(term.target); break;
                }
            at = match.capturedEnd();
        }
        return result + plain.mid(at);
    };
    while (matches.hasNext()) {
        const auto match = matches.next();
        plan.masked += protectPlain(source.mid(cursor, match.capturedStart() - cursor));
        plan.masked += mask(match.captured());
        cursor = match.capturedEnd();
    }
    plan.masked += protectPlain(source.mid(cursor));
    return plan;
}
QString restoreTranslation(const QString &output, const ProtectedText &plan) {
    if (output.trimmed().isEmpty() || output.size() > 24000 || !output.isValidUtf16())
        fail(QCoreApplication::translate("Anvil", "Модель повернула порожній або недійсний текст"));
    QString text = output.trimmed();
    // Validate every unique marker before substitution, including invented markers.
    auto residual = text;
    for (const auto &[token, replacement] : plan.replacements) {
        Q_UNUSED(replacement);
        if (text.count(token) != 1) fail(QCoreApplication::translate("Anvil", "Модель змінила або загубила підстановку/тег; результат відхилено"));
        residual.replace(token, "");
    }
    if (residual.contains("__ANVIL_")) fail(QCoreApplication::translate("Anvil", "Модель додала невідому службову позначку"));
    for (const auto &[token, replacement] : plan.replacements) text.replace(token, replacement);
    // A model must not invent additional game placeholders or markup.
    auto counts = [](const QString &s) {
        QMap<QString,int> result;
        auto it = QRegularExpression(tokensPattern()).globalMatch(s);
        while (it.hasNext()) ++result[it.next().captured()];
        return result;
    };
    QString expected;
    for (const auto &pair : plan.replacements) expected += pair.second;
    if (counts(text) != counts(expected)) fail(QCoreApplication::translate("Anvil", "Набір підстановок/тегів після перекладу змінився"));
    return text;
}
void TranslationJob::run() {
    try {
        QFile model(settings.model);
        if (!model.open(QIODevice::ReadOnly) || model.read(4) != "GGUF") fail(QCoreApplication::translate("Anvil", "Оберіть файл TranslateGemma GGUF"));
        model.close();
        QHash<QString,QString> cache;
        QFile journal;
        std::unique_ptr<QLockFile> cacheLock;
        if (!settings.cacheFile.isEmpty()) {
            const auto parent=QFileInfo(settings.cacheFile).absolutePath();
            if (QDir().mkpath(parent) && !QFileInfo(settings.cacheFile).isSymLink()) {
                cacheLock=std::make_unique<QLockFile>(settings.cacheFile+".lock");
                cacheLock->setStaleLockTime(0);
                if (cacheLock->tryLock(0)) {
                    journal.setFileName(settings.cacheFile);
                    if (journal.open(QIODevice::ReadOnly) && journal.size()<=512LL*1024*1024) {
                        while (!journal.atEnd() && !isInterruptionRequested()) {
                            const auto line=journal.readLine(256*1024);
                            if (!line.endsWith('\n')) {
                                while(!journal.atEnd()&&!journal.readLine(256*1024).endsWith('\n')) {}
                                continue;
                            }
                            const auto record=QJsonDocument::fromJson(line).object();
                            const auto key=record.value("key").toString(), text=record.value("output").toString();
                            if (key.size()==64 && !text.isEmpty() && text.size()<=24000) cache.insert(key,text);
                        }
                    }
                    journal.close();
                    if (QFileInfo(journal).size()<=512LL*1024*1024) {
                        if (journal.open(QIODevice::WriteOnly|QIODevice::Append) && journal.size()>0)
                            journal.write("\n"); // isolate a partial final record after an interrupted write
                    }
                }
            }
        }
        if (!settings.cacheFile.isEmpty() && !journal.isOpen())
            fail(QCoreApplication::translate("Anvil", "Не вдалося відкрити проміжний кеш: перевірте вільне місце/права або інший сеанс. Можна вимкнути збереження проміжних результатів."));
        const QFileInfo modelInfo(settings.model);
        QJsonArray terms;
        for (const auto &term:glossary) terms.append(QJsonObject{{"source",term.source},{"target",term.target}});
        const auto identity=QJsonDocument(QJsonObject{{"model",modelInfo.canonicalFilePath()},
            {"bytes",QString::number(modelInfo.size())},{"mtime",QString::number(modelInfo.lastModified().toMSecsSinceEpoch())},
            {"glossary",terms},{"format","anvil-en-uk-markers-v2"}}).toJson(QJsonDocument::Compact);
        if (!QFileInfo(settings.server).isExecutable()) fail(QCoreApplication::translate("Anvil", "Не знайдено виконуваний llama-server"));
        QTcpServer reservation;
        if (!reservation.listen(QHostAddress::LocalHost, 0)) fail(QCoreApplication::translate("Anvil", "Не вдалося вибрати локальний порт"));
        const auto port = reservation.serverPort();
        reservation.close();
        const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ServerProcess owned;
        auto &process = owned.process;
        process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_LINUX
        const auto ownerPid = getpid();
        process.setChildProcessModifier([ownerPid] {
            if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != ownerPid) _exit(125);
        });
#endif
        process.start(settings.server, {"--model", settings.model, "--alias", "anvil-translategemma",
            "--host", "127.0.0.1", "--port", QString::number(port), "--api-key", key,
            "--offline", "--jinja", "--ctx-size", "8192", "--parallel", "1",
            "--threads", QString::number(qBound(1, settings.threads, 64)),
            "--n-gpu-layers", settings.cuda ? "99" : "0", "--device", settings.cuda ? "CUDA0" : "none",
            "--batch-size", "256", "--ubatch-size", "128", "--no-webui"});
        if (!process.waitForStarted(5000)) fail(QCoreApplication::translate("Anvil", "Не вдалося запустити llama-server: ") + process.errorString());
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        auto request = [&](const QString &path, const QJsonObject &body, int timeout) {
            if (isInterruptionRequested()) fail(QCoreApplication::translate("Anvil", "Переклад зупинено"));
            QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1%2").arg(port).arg(path)));
            req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            auto *reply = body.isEmpty() ? network.get(req) : network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
            QEventLoop loop;
            QTimer limit, cancellation;
            limit.setSingleShot(true);
            QObject::connect(&limit, &QTimer::timeout, reply, &QNetworkReply::abort);
            QObject::connect(&cancellation, &QTimer::timeout, reply, [&] {
                if (isInterruptionRequested()) reply->abort();
                // Drain engine logs to avoid an unbounded output queue during long batches.
                process.readAll();
            });
            QObject::connect(reply, &QNetworkReply::readyRead, reply, [&] {
                if (reply->bytesAvailable() > 2 * 1024 * 1024) reply->abort();
            });
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            limit.start(timeout); cancellation.start(100);
            loop.exec();
            const auto data = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = reply->error() == QNetworkReply::NoError && status == 200 && data.size() <= 2*1024*1024;
            delete reply;
            if (!ok) fail(isInterruptionRequested() ? QCoreApplication::translate("Anvil", "Переклад зупинено") : QCoreApplication::translate("Anvil", "Локальна модель не відповіла або повернула помилку"));
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson(data, &error);
            if (error.error != QJsonParseError::NoError || !json.isObject()) fail(QCoreApplication::translate("Anvil", "Недійсна відповідь локальної моделі"));
            return json.object();
        };
        emit progress(QCoreApplication::translate("Anvil", "Завантаження TranslateGemma 4B…"));
        QElapsedTimer loading; loading.start();
        bool ready = false;
        while (loading.elapsed() < 180000 && !isInterruptionRequested()) {
            if (process.state() == QProcess::NotRunning) fail(QCoreApplication::translate("Anvil", "Рушій завершився. Спробуйте режим CPU або перевірте шлях до моделі."));
            try { request("/health", {}, 1000); ready = true; break; } catch (const std::exception &) {}
            QThread::msleep(100);
        }
        if (!ready) fail(isInterruptionRequested() ? QCoreApplication::translate("Anvil", "Переклад зупинено") : QCoreApplication::translate("Anvil", "Завантаження моделі перевищило 180 секунд"));
        const auto props = request("/props", {}, 5000);
        const auto tmpl = props.value("chat_template").toString();
        if (!tmpl.contains("source_lang_code") || !tmpl.contains("target_lang_code"))
            fail(QCoreApplication::translate("Anvil", "GGUF не містить спеціального шаблону TranslateGemma"));
        for (qsizetype i=0; i<entries.size() && !isInterruptionRequested(); ++i) {
            emit progress(QString(QCoreApplication::translate("Anvil", "Переклад %1 із %2…")).arg(i+1).arg(entries.size()));
            try {
                const auto plan = protectTranslation(entries[i].source, glossary);
                const auto key=QString::fromLatin1(QCryptographicHash::hash(identity+'\0'+entries[i].source.toUtf8(),QCryptographicHash::Sha256).toHex());
                QString rawOutput;
                if (cache.contains(key)) {
                    try { restoreTranslation(cache.value(key),plan); rawOutput=cache.value(key); }
                    catch (const std::exception &) { cache.remove(key); }
                }
                if (rawOutput.isEmpty()) {
                const auto reply = request("/v1/chat/completions", {
                    {"model", "anvil-translategemma"},
                    {"messages", QJsonArray{QJsonObject{{"role","user"},{"content",plan.masked}}}},
                    {"chat_template_kwargs", QJsonObject{{"source_lang_code","en"},{"target_lang_code","uk"}}},
                    {"temperature",0.0}, {"seed",0}, {"max_tokens",4096}, {"stream",false}}, 180000);
                const auto choices = reply.value("choices").toArray();
                if (choices.size()!=1 || choices[0].toObject().value("finish_reason").toString()!="stop")
                    fail(QCoreApplication::translate("Anvil", "Відповідь моделі неповна; результат відхилено"));
                rawOutput=choices[0].toObject().value("message").toObject().value("content").toString();
                restoreTranslation(rawOutput,plan);
                if (journal.isOpen()) {
                    const auto record=QJsonDocument(QJsonObject{{"key",key},{"output",rawOutput}}).toJson(QJsonDocument::Compact)+'\n';
                    if (journal.write(record)!=record.size() || !journal.flush()) {
                        journal.close();
                        fail(QCoreApplication::translate("Anvil", "Не вдалося зберегти проміжний результат. Чергу зупинено; попередні готові переклади можна застосувати."));
                    }
                }
                cache.insert(key,rawOutput);
                }
                auto text = restoreTranslation(rawOutput, plan);
                const QRegularExpression leading("^\\s*"), trailing("\\s*$");
                text = leading.match(entries[i].source).captured() + text.trimmed() +
                       trailing.match(entries[i].source).captured();
                emit result(i, text, {});
            } catch (const std::exception &e) {
                if (!settings.cacheFile.isEmpty() && !journal.isOpen()) {
                    emit failure(QString::fromUtf8(e.what()));
                    return;
                }
                if (!isInterruptionRequested()) emit result(i, {}, QString::fromUtf8(e.what()));
            }
        }
        emit progress(isInterruptionRequested() ? QCoreApplication::translate("Anvil", "Зупинено. Модель вивантажується…") : QCoreApplication::translate("Anvil", "Готово. Модель вивантажується…"));
    } catch (const std::exception &e) {
        if (!isInterruptionRequested()) emit failure(QString::fromUtf8(e.what()));
    }
}
} // namespace anvil
