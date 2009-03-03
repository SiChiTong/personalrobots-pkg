#include <ros/node.h>
#include <image_msgs/Image.h>
#include <image_msgs/CamInfo.h>
#include <image_msgs/CvBridge.h>
#include <robot_msgs/PoseStamped.h>
#include <prosilica_cam/PolledImage.h>
#include <tf/transform_broadcaster.h>

#include <LinearMath/btVector3.h>
#include <LinearMath/btMatrix3x3.h>

#include <opencv/cv.h>
#include <opencv/highgui.h>

#include <boost/thread.hpp>

#include "outlet_detector.h"

//#define _OUTLET_INTERACTIVE_CAPTURE

class OutletDetector : public ros::Node
{
private:
  static const char wndname[];

  prosilica_cam::PolledImage::Request req_;
  prosilica_cam::PolledImage::Response res_;
  image_msgs::Image& img_;
  image_msgs::CamInfo& cam_info_;
  image_msgs::CvBridge img_bridge_;
  robot_msgs::PoseStamped pose_;
  tf::TransformBroadcaster tf_broadcaster_;
  CvMat* K_;
  bool display_;
  int count_;
  IplImage* display_image_;
  bool continue_, failed_;
  btVector3 holes[12];
  btVector3 old_holes[12];

public:
  OutletDetector()
    : ros::Node("outlet_detector"), img_(res_.image), cam_info_(res_.cam_info),
      tf_broadcaster_(*this), K_(NULL), count_(0), display_image_(NULL), continue_(false)
  {
    param("display", display_, false);
    if (display_) {
      cvNamedWindow(wndname, 0); // no autosize
      cvStartWindowThread();
    }
    
    advertise<robot_msgs::PoseStamped>("pose", 1);
  }

  ~OutletDetector()
  {
    cvReleaseMat(&K_);
    cvReleaseImage(&display_image_);
    if (display_)
      cvDestroyWindow(wndname);
  }

  void caminfo_cb()
  {
    if (K_ == NULL)
      K_ = cvCreateMat(3, 3, CV_64FC1);
    memcpy((char*)(K_->data.db), (char*)(&cam_info_.K[0]), 9 * sizeof(double));
  }

  void image_cb()
  {
    if (!img_bridge_.fromImage(img_, "bgr")) {
      ROS_ERROR("Failed to convert image");
      return;
    }

    IplImage* image = img_bridge_.toIpl();
    std::vector<outlet_t> outlets;
    if (!detect_outlet_tuple(image, K_, NULL, outlets)) {
      ROS_WARN("Failed to detect outlet");
      failed_ = true;
      if (display_)
        cvShowImage(wndname, image);
      return;
    }
    failed_ = false;

    // Change representation and coordinate frame
    //btVector3 holes[12];
    for (int i = 0; i < 4; ++i) {
      changeAxes(outlets[i].coord_hole_ground, holes[3*i]);
      changeAxes(outlets[i].coord_hole1, holes[3*i+1]);
      changeAxes(outlets[i].coord_hole2, holes[3*i+2]);
    }

    // Find normal and right vectors
    // TODO: fit plane to all holes instead of just 3
    btVector3 right = (holes[2] - holes[1]).normalized();
    btVector3 normal = right.cross(holes[2] - holes[0]).normalized();
    // TODO: right-handed???
    btVector3 up = right.cross(normal).normalized();
    btMatrix3x3 rotation;
    rotation[1] = -right;
    rotation[2] = up;
    rotation[0] = normal;
    rotation = rotation.transpose();
    btQuaternion orientation;
    rotation.getRotation(orientation);
    
    pose_.header.frame_id = "high_def_frame";
    pose_.pose.position.x = holes[0].x();
    pose_.pose.position.y = holes[0].y();
    pose_.pose.position.z = holes[0].z();
    pose_.pose.orientation.x = orientation.x();
    pose_.pose.orientation.y = orientation.y();
    pose_.pose.orientation.z = orientation.z();
    pose_.pose.orientation.w = orientation.w();

    publish("pose", pose_);
    tf_broadcaster_.sendTransform(tf::Transform(orientation, holes[0]),
                                  ros::Time::now(), "outlet_frame",
                                  "high_def_frame");
    
    ROS_INFO("Hole 0: %.5f %.5f %.5f, Hole 1: %.5f %.5f %.5f, Hole 2: %.5f %.5f %.5f",
             holes[0].x(), holes[0].y(), holes[0].z(),
             holes[1].x(), holes[1].y(), holes[1].z(),
             holes[2].x(), holes[2].y(), holes[2].z());
    
    
    if (display_) {
#ifdef _OUTLET_INTERACTIVE_CAPTURE
      if (!display_image_)
        display_image_ = cvCloneImage(image);
      else
        cvCopy(image, display_image_);
      draw_outlets(display_image_, outlets);
      cvShowImage(wndname, display_image_);
#else
      draw_outlets(image, outlets);
      cvShowImage(wndname, image);
#endif
    }
  }

  void saveImages()
  {
    char buffer[32];
    snprintf(buffer, 32, "views/V%04d.jpg", count_);
    cvSaveImage(buffer, img_bridge_.toIpl());
    printf("Saved %s\n", buffer);
    snprintf(buffer, 32, "views/out%04d.jpg", count_++);
    cvSaveImage(buffer, display_image_);
    printf("Saved %s\n", buffer);
  }
  
  bool spin()
  {
    // TODO: wait for service to become available
    while (ok())
    {
      req_.timeout_ms = 100;
      if (ros::service::call("/prosilica/poll", req_, res_)) {
        caminfo_cb();
        image_cb();
#ifdef _OUTLET_INTERACTIVE_CAPTURE
        if (continue_) {
          int i = 0;
          while (!failed_ && i < 12) {
            if (holes[i].distance(old_holes[i]) > 0.01)
              failed_ = true;
            ++i;
          }

          if (failed_)
            continue_ = false;
        }
        if (!continue_) {
          int key = cvWaitKey(0);
          if (key == 's') {
            saveImages();
          } else if (key == 'c') {
            memcpy(old_holes, holes, sizeof(holes));
            continue_ = true;
          } else if (key == 'q') {
            return true;
          }
        }
#endif
      } else {
        ROS_WARN("Service call failed");
        usleep(100000);
      }
    }

    return true;
  }

private:
  static void changeAxes(CvPoint3D32f src, btVector3 &dst)
  {
    // Scale to meters while we're at it
    dst.setValue(src.z / 1000.0, -src.x / 1000.0, -src.y / 1000.0);
  }
};

const char OutletDetector::wndname[] = "Outlet detector";

int main(int argc, char **argv)
{
  ros::init(argc, argv);
  OutletDetector detector;
  detector.spin();

  return 0;
}
