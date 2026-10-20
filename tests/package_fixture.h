#pragma once
#include <anvil/package.h>
#include <anvil/breakpoint.h>
#include <anvil/wildlands.h>
#include <anvil/storage.h>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <stdexcept>
namespace anvil::test {
template<class T>void le(QByteArray &b,T n){char d[sizeof(T)];qToLittleEndian(n,d);b.append(d,sizeof(T));}
template<class T>void be(QByteArray &b,T n){char d[sizeof(T)];qToBigEndian(n,d);b.append(d,sizeof(T));}
inline QByteArray payload(){
    QByteArray d;be<quint16>(d,2);be<quint16>(d,5);
    for(const auto &p:QVector<QPair<quint16,quint16>>{{0,0},{'A',0},{0x416,0},{1,2},{3,1}}){be(d,p.first);be(d,p.second);}
    be<quint16>(d,1);be<quint32>(d,100);be<quint32>(d,46);be<quint32>(d,38);
    be<quint16>(d,1);be<quint16>(d,1);be<quint16>(d,7);be<quint16>(d,6);d+=QByteArray::fromHex("000200ff0003");
    QByteArray p;le<quint64>(p,12345);le<quint32>(p,0x6e3c9c6f);
    p+=QByteArray::fromHex("01000000000100000000000000000000f800000000b58983d2");le<quint32>(p,d.size());return p+d;
}
inline QByteArray stream(const QByteArray &data,bool wild){
    QByteArray b;le<quint64>(b,0x1004fa9957fbaa33ULL);le<quint16>(b,wild?1:3);b+=char(wild?1:3);
    le<quint16>(b,32768);le<quint16>(b,32768);le<quint32>(b,1);
    if(wild){le<quint16>(b,data.size());le<quint16>(b,data.size());}else{le<quint32>(b,data.size());le<quint32>(b,data.size());}
    quint32 a=0,s=0;for(unsigned char c:data){a=(a+c)%65521;s=(s+a)%65521;}le<quint32>(b,s<<16|a);return b+data;
}
inline QByteArray forge(bool wild){
    const QByteArray name="LocalizationPackage_English(US)";const auto p=payload();QByteArray files;
    le<quint32>(files,0x6e3c9c6f);le<quint32>(files,p.size());le<quint32>(files,name.size());files+=name;files+='\0';files+=p;
    QByteArray metadata;le<quint16>(metadata,1);le<quint64>(metadata,12345);le<quint32>(metadata,files.size());le<quint16>(metadata,0);
    const auto data=stream(metadata,wild)+stream(files,wild);
    QByteArray b(8192,'\0');b.replace(0,9,QByteArray("scimitar\0",9));
    qToLittleEndian<quint32>(27,b.data()+9);qToLittleEndian<quint64>(1050,b.data()+13);
    qToLittleEndian<quint32>(1,b.data()+1050);qToLittleEndian<qint32>(-1,b.data()+1070);qToLittleEndian<qint32>(-1,b.data()+1074);
    qToLittleEndian<quint32>(1,b.data()+1082);qToLittleEndian<qint64>(1094,b.data()+1086);
    qToLittleEndian<quint32>(1,b.data()+1094);qToLittleEndian<qint64>(1142,b.data()+1102);
    qToLittleEndian<qint64>(-1,b.data()+1110);qToLittleEndian<qint64>(1162,b.data()+1126);
    qToLittleEndian<qint64>(4096,b.data()+1142);qToLittleEndian<quint64>(12345,b.data()+1150);
    qToLittleEndian<qint32>(data.size(),b.data()+1158);qToLittleEndian<qint32>(data.size(),b.data()+1162);
    b.replace(1166,8,"OPAQUE!!");qToLittleEndian<quint32>(0x6e3c9c6f,b.data()+1178);b.replace(1206,name.size(),name);b.replace(4096,data.size(),data);return b;
}
inline void write(const QString &path,const QByteArray &b){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(b)!=b.size())throw std::runtime_error("fixture write");}
inline anvil::Document project(bool wild){auto d=wild?anvil::wildlands::decodeLocalization(payload()):anvil::breakpoint::decodeLocalization(payload());d.entries[0].translation="Налаштування";d.entries[0].status="translated";return d;}
inline void game(const QString &root,bool wild){QDir().mkpath(root);write(root+(wild?"/GRW.exe":"/GRB.exe"),"fixture");write(root+"/DataPC.forge",forge(wild));write(root+"/DataPC_patch_01.forge",forge(wild));}
}
