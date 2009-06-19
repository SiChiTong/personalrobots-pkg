#include "Python.h"

#include "star_detector/detector.h"
#include <cvwimage.h> // Google C++ wrappers
#include <highgui.h>
#include <cassert>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <ostream>

typedef struct {
    PyObject_HEAD
    StarDetector *psd;
    int xsize, ysize;
    IplImage *img;
} star_detector_t;

static void
star_detector_dealloc(PyObject *self)
{
    star_detector_t *psdt = (star_detector_t*)self;
    cvReleaseImage(&(psdt->img));
    delete psdt->psd;
    PyObject_Del(self);
}

PyObject *detect(PyObject *self, PyObject *args)
{
    star_detector_t *sd = (star_detector_t*)self;
    Py_ssize_t im_size;
    char *imdata;
    if (!PyArg_ParseTuple(args, "s#", &imdata, &im_size))
      return NULL;
    if (im_size != (sd->xsize * sd->ysize)) {
        PyErr_SetString(PyExc_TypeError, "Image size mismatch");
        return NULL;
    }

    if (sd->img->widthStep == sd->xsize) {
        memcpy(sd->img->imageData, imdata, sd->xsize * sd->ysize);
    } else {
        for (int y = 0; y < sd->ysize; y++) {
            memcpy(&CV_IMAGE_ELEM(sd->img, char, y, 0), imdata + y * (sd->xsize), sd->xsize);
        }
    }
    std::vector<Keypoint> kp;
    sd->psd->DetectPoints(sd->img, std::back_inserter(kp));
    PyObject *r = PyList_New(kp.size());
    for (size_t i = 0; i < kp.size(); i++) {
        PyObject *t = PyTuple_New(4);
        PyTuple_SetItem(t, 0, PyInt_FromLong(kp[i].x));
        PyTuple_SetItem(t, 1, PyInt_FromLong(kp[i].y));
        PyTuple_SetItem(t, 2, PyInt_FromLong(kp[i].scale));
        PyTuple_SetItem(t, 3, PyFloat_FromDouble(kp[i].response));
        PyList_SetItem(r, i, t);
    }
    return r;
}

PyObject *setScales(PyObject *self, PyObject *args)
{
    star_detector_t *sd = (star_detector_t*)self;
    int scales;
    if (!PyArg_ParseTuple(args, "i", &scales))
        return NULL;
    sd->psd->setScales(scales);
    Py_RETURN_NONE;
}

PyObject *setResponseThreshold(PyObject *self, PyObject *args)
{
    star_detector_t *sd = (star_detector_t*)self;
    double t;
    if (!PyArg_ParseTuple(args, "d", &t))
        return NULL;
    sd->psd->setResponseThreshold(t);
    Py_RETURN_NONE;
}

PyObject *setProjectedThreshold(PyObject *self, PyObject *args)
{
    star_detector_t *sd = (star_detector_t*)self;
    double t;
    if (!PyArg_ParseTuple(args, "d", &t))
        return NULL;
    sd->psd->setProjectedThreshold(t);
    Py_RETURN_NONE;
}

PyObject *setBinarizedThreshold(PyObject *self, PyObject *args)
{
    star_detector_t *sd = (star_detector_t*)self;
    double t;
    if (!PyArg_ParseTuple(args, "d", &t))
        return NULL;
    sd->psd->setBinarizedThreshold(t);
    Py_RETURN_NONE;
}

/* Method table */
static PyMethodDef star_detector_methods[] = {
  {"detect", detect, METH_VARARGS},
  {"setScales", setScales, METH_VARARGS},
  {"setResponseThreshold", setResponseThreshold, METH_VARARGS},
  {"setProjectedThreshold", setProjectedThreshold, METH_VARARGS},
  {"setBinarizedThreshold", setBinarizedThreshold, METH_VARARGS},
  {NULL, NULL},
};

static PyObject *
star_detector_GetAttr(PyObject *self, char *attrname)
{
    return Py_FindMethod(star_detector_methods, self, attrname);
}

static PyTypeObject star_detector_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "star_detector",
    sizeof(star_detector_t),
    0,
    (destructor)star_detector_dealloc,
    0,
    (getattrfunc)star_detector_GetAttr,
    0,
    0,
    0, // repr
    0,
    0,
    0,

    0,
    0,
    0,
    0,
    0,
    
    0,
    
    Py_TPFLAGS_CHECKTYPES,

    0,
    0,
    0,
    0

    /* the rest are NULLs */
};

PyObject *star_detector(PyObject *self, PyObject *args)
{
    star_detector_t *object = PyObject_NEW(star_detector_t, &star_detector_Type);
    int scales = 7;
    double threshold = 30.0;
    double line_threshold = 10.0;
    double line_threshold_bin = 8.0;
    if (!PyArg_ParseTuple(args, "ii|iddd", &object->xsize, &object->ysize, &scales, &threshold, &line_threshold, &line_threshold_bin))
        return NULL;
    object->psd = new StarDetector( cvSize(object->xsize, object->ysize), scales, threshold, line_threshold, line_threshold_bin );
    object->img = cvCreateImage(cvSize(object->xsize, object->ysize), IPL_DEPTH_8U, 1);
    return (PyObject*)object;
}

static PyMethodDef methods[] = {
  {"star_detector", star_detector, METH_VARARGS},
  {NULL, NULL},
};

extern "C" void initstarfeature()
{
    PyObject *m, *d;

    m = Py_InitModule("starfeature", methods);
    d = PyModule_GetDict(m);
}
#if 0
    cv::WImageBuffer1_b reference( cvLoadImage("im640x480.pgm", CV_LOAD_IMAGE_GRAYSCALE) );

    // Detect points in reference image
    StarDetector ref_detector( cvSize(reference.Width(), reference.Height()) );

    struct timeval started, finished;
    gettimeofday(&started, NULL);

    int ITER = 200;
    for (int i = 0; i < ITER; i++) {
        std::vector<Keypoint> ref_keypts;
        ref_detector.DetectPoints(reference.Ipl(), std::back_inserter(ref_keypts));
    }
    gettimeofday(&finished, NULL);
    long long a = started.tv_sec * 1000000 + started.tv_usec;
    long long b = finished.tv_sec * 1000000 + finished.tv_usec;
    float took = (b - a) * 1.0e-6;
    printf("took %f s, so %f ms/image\n", took, 1000 * took / ITER);
#endif
