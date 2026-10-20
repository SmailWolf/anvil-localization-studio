#include "fontdialog.h"
#include <anvil/fonts.h>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <exception>
#include <memory>
FontDialog::FontDialog(QWidget *parent):QDialog(parent) {
    setObjectName("fontDialog");setWindowTitle(QCoreApplication::translate("Anvil","Шрифти — Українська + Беларуская"));resize(840,720);
    auto *layout=new QVBoxLayout(this);
    auto *notice=new QLabel(QCoreApplication::translate("Anvil","Noto Sans включає обидві абетки. Перевірте шрифт і збережіть набір поза грою. "
        "TTF не є готовим патчем. Генерація GFOF — експериментальна; показ у грі та шрифти Wildlands ще не підтверджено."),this);
    notice->setWordWrap(true);layout->addWidget(notice);
    auto *form=new QFormLayout;layout->addLayout(form);
    auto *language=new QComboBox(this);language->setObjectName("fontLanguage");
    language->addItem(QCoreApplication::translate("Anvil","Українська + Беларуская"),"both");language->addItem(QCoreApplication::translate("Anvil","Українська"),"uk");language->addItem("Беларуская","be");
    form->addRow(QCoreApplication::translate("Anvil","Перевіряти літери"),language);
    auto *font=new QLineEdit(anvil::fonts::bundledFont(),this);font->setReadOnly(true);font->setProperty("fontPath",anvil::fonts::bundledFont());font->setText("Noto Sans");font->setObjectName("fontSource");
    auto *row=new QWidget(this);auto *buttons=new QHBoxLayout(row);buttons->setContentsMargins(0,0,0,0);buttons->addWidget(font);
    auto *browse=new QPushButton(QCoreApplication::translate("Anvil","Власний TTF / OTF…"),row);buttons->addWidget(browse);
    auto *reset=new QPushButton("Noto Sans",row);buttons->addWidget(reset);form->addRow(QCoreApplication::translate("Anvil","Шрифт"),row);
    auto *donor=new QLineEdit(this);donor->setObjectName("fontDonor");
    auto *donorRow=new QWidget(this);auto *donorBox=new QHBoxLayout(donorRow);donorBox->setContentsMargins(0,0,0,0);donorBox->addWidget(donor);
    auto *pickDonor=new QPushButton(QCoreApplication::translate("Anvil","Обрати…"),donorRow);donorBox->addWidget(pickDonor);
    form->addRow(QCoreApplication::translate("Anvil","Донор GFOF (необов’язково)"),donorRow);
    donor->setPlaceholderText(QCoreApplication::translate("Anvil","Окремо декодований GFOF v3.3; не .forge"));
    auto *size=new QSpinBox(this);size->setRange(20,100);size->setValue(50);form->addRow(QCoreApplication::translate("Anvil","Розмір гліфа (px)"),size);
    auto *spread=new QSpinBox(this);spread->setRange(1,16);spread->setValue(5);form->addRow("SDF spread",spread);
    auto *preview=new QLabel(this);preview->setObjectName("fontPreview");preview->setWordWrap(true);preview->setMinimumHeight(140);layout->addWidget(preview);
    auto *report=new QPlainTextEdit(this);report->setReadOnly(true);report->setObjectName("fontReport");layout->addWidget(report,1);
    auto *footer=new QDialogButtonBox(QDialogButtonBox::Close,this);footer->button(QDialogButtonBox::Close)->setText(QCoreApplication::translate("Anvil","Закрити"));layout->addWidget(footer);
    auto *save=footer->addButton(QCoreApplication::translate("Anvil","Зберегти шрифтовий набір…"),QDialogButtonBox::ActionRole);
    save->setObjectName("exportFontKit");
    auto fontId=std::make_shared<int>(-1);
    connect(this,&QObject::destroyed,[fontId]{if(*fontId>=0)QFontDatabase::removeApplicationFont(*fontId);});
    const auto refresh=[=,this]{
        try {
            const auto data=anvil::fonts::inspect(font->property("fontPath").toString(),language->currentData().toString());
            report->setPlainText(data["family"].toString()+"\n\n"+QCoreApplication::translate("Anvil","Відсутні літери: %1").arg(data["missingCharacters"].toString().isEmpty()?QCoreApplication::translate("Anvil","немає"):data["missingCharacters"].toString())+"\n\n"+QCoreApplication::translate("Anvil","Перевіряються обрана абетка, латиниця, цифри й апострофи. Звіт validation.json буде збережено разом зі шрифтом."));
            if(*fontId>=0)QFontDatabase::removeApplicationFont(*fontId);
            const auto id=QFontDatabase::addApplicationFont(font->property("fontPath").toString());
            *fontId=id;
            if(id>=0){const auto families=QFontDatabase::applicationFontFamilies(id);if(!families.isEmpty()){QFont f(families.first());f.setPixelSize(27);preview->setFont(f);}}
            preview->setText(QString::fromUtf8("Українська: Налаштування · Ґґ Єє Іі Її\nБеларуская: Налады · Ёё Іі Ўў · аб’ект\n"));
            save->setEnabled(data["coverageVerified"].toBool());
        }catch(const std::exception &e){report->setPlainText(QString::fromUtf8(e.what()));save->setEnabled(false);}
    };
    connect(browse,&QPushButton::clicked,this,[=,this]{const auto p=QFileDialog::getOpenFileName(this,QCoreApplication::translate("Anvil","Шрифт"),{},"Fonts (*.ttf *.otf)");if(!p.isEmpty()){font->setText(p);font->setProperty("fontPath",p);refresh();}});
    connect(reset,&QPushButton::clicked,this,[=,this]{font->setText("Noto Sans");font->setProperty("fontPath",anvil::fonts::bundledFont());refresh();});
    connect(language,&QComboBox::currentIndexChanged,this,[=,this]{refresh();});
    connect(pickDonor,&QPushButton::clicked,this,[=,this]{const auto p=QFileDialog::getOpenFileName(this,"GFOF v3.3",{},"Raw GFOF (*.gfof);;All files (*)");if(!p.isEmpty())donor->setText(p);});
    connect(footer,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(save,&QPushButton::clicked,this,[=,this]{
        auto path=QFileDialog::getSaveFileName(this,QCoreApplication::translate("Anvil","Нова папка шрифтового набору поза грою"),QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/Anvil-fonts-uk-be",{},nullptr,QFileDialog::DontConfirmOverwrite);
        if(path.isEmpty())return;
        try {
            save->setEnabled(false);
            anvil::fonts::exportKit(font->property("fontPath").toString(),path,language->currentData().toString(),donor->text().trimmed(),size->value(),spread->value());
            QMessageBox::information(this,windowTitle(),QCoreApplication::translate("Anvil","Набір збережено: %1\nФайли гри не змінено. Пакувальник перекладу не вставляє цей шрифт у гру.").arg(path));
        }catch(const std::exception &e){QMessageBox::warning(this,windowTitle(),QString::fromUtf8(e.what()));}
        save->setEnabled(true);
    });
    refresh();
}
