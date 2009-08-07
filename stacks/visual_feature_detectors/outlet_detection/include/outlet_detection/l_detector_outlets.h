
#if !defined(_L_OUTLET_DETECTOR_H)
#define _L_OUTLET_DETECTOR_H


#include "outlet_detection/outlet_model.h"
#include <cvaux.h>
using namespace cv;

void read_training_base(const char* config_path, char* outlet_filename, 
					 vector<feature_t>& train_features);

void l_detector_initialize(PlanarObjectDetector& detector, LDetector& ldetector, const char* outlet_config_path, Mat& object,Size patchSize);

int l_detect_outlets(Mat& _image, Mat& object, const char* outlet_config_path, PlanarObjectDetector& detector, LDetector& ldetector, vector<feature_t>& train_features,
                            vector<outlet_t>& holes, const char* output_path = 0, const char* output_filename = 0);

#endif