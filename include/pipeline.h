#ifndef PROJECT_PIPELINE_H
#define PROJECT_PIPELINE_H

#include <glib.h>
#include <gst/gst.h>
#include <math.h>
#include <thread>
#include <iostream>
#include "gstnvdsmeta.h"

#define PGIE_CONFIG_FILE "../configs/pgie_detector.txt"
#define PGIE_CLASS_ID_PERSON 2
#define MAX_DISPLAY_LEN 64

#define GST_CAPS_FEATURES_NVMM "memory:NVMM"
#define MUXER_OUTPUT_WIDTH 1280
#define MUXER_OUTPUT_HEIGHT 720
#define MUXER_BATCH_TIMEOUT_USEC 40000

using namespace std;

class Pipeline
{
public:
    GMainLoop *loop = NULL;
    GstElement *pipeline = NULL, *streammux = NULL, *sink = NULL, *pgie = NULL,
        *nvvidconv = NULL, *nvosd = NULL;

    GstBus *bus = NULL;
    guint bus_watch_id;
    GstPad *osd_src_pad = NULL;

    guint pgie_batch_size;

    void createElements();

    void Verify();

    void Configure();

    void ConstructPipeline();

    void stopPipeline();

    static gboolean BusCall(GstBus *bus, GstMessage *msg, gpointer data);

    Pipeline();
    
    ~Pipeline();
};

#endif //PROJECT_PIPELINE_H
