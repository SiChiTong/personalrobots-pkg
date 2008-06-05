#include <cstdio>
#include <vector>
#include "opencv/cxcore.h"
#include "opencv/cv.h"
#include "opencv/highgui.h"
#include "ros/node.h"
#include "std_msgs/MsgImage.h"
#include "image_utils/cv_bridge.h"

#include <sys/stat.h>

using namespace std;

class CvView : public ros::node
{
public:
  MsgImage image_msg;
  CvBridge<MsgImage> cv_bridge;

  ros::thread::mutex cv_mutex;

  IplImage *cv_image;
  IplImage *cv_image_sc;

  char dir_name[256];
  int img_cnt;
  bool made_dir;

  CvView() : node("cv_view"), cv_bridge(&image_msg), cv_image(0), cv_image_sc(0), img_cnt(0), made_dir(false)
  { 
    cvNamedWindow("cv_view", CV_WINDOW_AUTOSIZE);
    subscribe("image", image_msg, &CvView::image_cb, 1);

    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    sprintf(dir_name, "%s_images_%.2d%.2d%.2d_%.2d%.2d%.2d", name.c_str()+1, timeinfo->tm_mon + 1, timeinfo->tm_mday,timeinfo->tm_year - 100,timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  }

 ~CvView()
  {
    free_images();
  }

  void free_images()
  {
    if (cv_image)
      cvReleaseImage(&cv_image);

    if (cv_image_sc)
      cvReleaseImage(&cv_image_sc);
  }

  void image_cb()
  {
    cv_mutex.lock();

    free_images();

    if (cv_bridge.to_cv(&cv_image))
    {
      cv_image_sc = cvCreateImage(cvSize(cv_image->width, cv_image->height),
				  IPL_DEPTH_8U, 1);

      if (cv_image->depth == IPL_DEPTH_16U)
	cvCvtScale(cv_image, cv_image_sc, 0.0625, 0);
      else
	cvCvtScale(cv_image, cv_image_sc, 1, 0);
      
      cvShowImage("cv_view", cv_image_sc);
    }
    cv_mutex.unlock();
  }

  void check_keys() {
    cv_mutex.lock();
    if (cvWaitKey(3) == 10)
      save_image();
    cv_mutex.unlock();
  }

  void save_image() {

    if (!made_dir) {
      if (mkdir(dir_name, 0755)) {
	printf("Failed to make directory: %s\n", dir_name);
	return;
      } else {
	made_dir = true;
      }
    }

    std::ostringstream oss;
    oss << dir_name << "/Img" << img_cnt++ << ".png";
    cvSaveImage(oss.str().c_str(), cv_image_sc);
    
  }
};

int main(int argc, char **argv)
{
  ros::init(argc, argv);
  CvView view;
  while (view.ok()) {
    usleep(10000);
    view.check_keys();
  }
  ros::fini();
  return 0;
}

