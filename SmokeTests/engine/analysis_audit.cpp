#include <QCoreApplication>
#include <QTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QElapsedTimer>
#include "Editor/AnalysisThread.h"
#include <cmath>
#include <memory>
#include <iostream>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=4 && app.arguments().size()!=5)return 2;
    const auto config=app.arguments()[1],output=app.arguments()[2];const int frames=app.arguments()[3].toInt();
    if(frames<1024 || frames>1048576)return 2;
    auto worker=std::make_unique<AnalysisThread>();QElapsedTimer elapsed;elapsed.start();
    QObject::connect(worker.get(),&AnalysisThread::analysisFinished,&app,[&]() {
        worker->beginGetResult();const unsigned status=worker->getAnalysisStatus();
        const auto revision=worker->getResultRevision(),requested=worker->getRequestRevision();
        if(revision!=requested || (app.arguments().size()==5 && requested<2)) {worker->endGetResult();return;}
        const int count=worker->getFreqDataLength(),rate=worker->getFreqDataSampleRate();
        QJsonArray response;
        if(!status)for(double frequency:{20.,80.,100.,500.,997.,1000.,2000.,10000.,20000.}) {
            const int bin=(std::max)(0,(std::min)(count/2-1,int(std::round(frequency*count/rate))));
            const auto* data=worker->getFreqData();const double magnitude=std::hypot(data[bin][0],data[bin][1]);
            const double gain=20*std::log10(magnitude);
            response.append(QJsonObject{{"hz",frequency},{"gain_db",std::isfinite(gain)?QJsonValue(gain):QJsonValue()}});
        }
        const double peak=worker->getPeakGain();
        QJsonObject report{{"status",status?"ANALYZER_LIMITATION":"PASS"},{"analysis_status",int(status)},
            {"peak_db",std::isfinite(peak)?QJsonValue(peak):QJsonValue()},
            {"latency_samples",worker->getLatency()},{"frames",count},{"sample_rate",rate},
            {"wall_processing_ms",worker->getProcessingTime()},{"cpu_processing_ms",worker->getProcessingCpuTime()},
            {"elapsed_ms",elapsed.elapsed()},{"response",response},
            {"result_revision",QString::number(revision)},{"request_revision",QString::number(requested)},
            {"limits","Actual Editor AnalysisThread and isolated clone; no GUI, installed audiodg or arbitrary nonlinear response certification"}};
        worker->endGetResult();QFile file(output);if(!file.open(QIODevice::WriteOnly))std::exit(3);
        file.write(QJsonDocument(report).toJson());file.close();
        worker.reset();std::cout<<"analysis_status="<<status<<" elapsed_ms="<<elapsed.elapsed()<<'\n';app.exit(status?1:0);
    });
    QTimer::singleShot(20000,&app,[](){std::cerr<<"ANALYSIS_TEST_TIMEOUT\n";std::exit(4);});
    worker->start();worker->setParameters(nullptr,0,0,config,frames);
    if(app.arguments().size()==5)QTimer::singleShot(1,&app,[&](){
        worker->setParameters(nullptr,0,0,app.arguments()[4],frames);
    });
    return app.exec();
}
