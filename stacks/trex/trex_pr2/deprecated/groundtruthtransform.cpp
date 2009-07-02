
#include "ros/node.h"
#include "robot_msgs/Point32.h"
#include "tf/transform_listener.h"

using std::string;

/**
 * This is a hack to get the position of the robot. TfPy is not here yet, so this is just to get the tests working.
 */
class GroundTruthTransform : public ros::Node {
public:
  robot_msgs::Point32 msg;
  tf::TransformListener tf;

  GroundTruthTransform() : ros::Node("GroundTruthTransform"), tf(*this, true, ros::Duration(10)) {
    advertise<robot_msgs::Point32>("groundtruthposition", 1);
  }

  void speak() {
    tf::Stamped<tf::Pose> robotPose, globalPose;
    robotPose.setIdentity();
    robotPose.frame_id_ = "base_link";
    robotPose.stamp_ = ros::Time();
    try {
      this->tf.transformPose("map", robotPose, globalPose);
    } catch(tf::LookupException& ex) {
      std::cerr << "LookupException in transformPose(\"map\", robotPose, globalPose): " << ex.what() << "\n";
      std::cout << "LookupException in transformPose(\"map\", robotPose, globalPose): " << ex.what();
      return;
    } catch(tf::ExtrapolationException& ex) {
      std::cerr << "ExtrapolateException in transformPose(\"map\", robotPose, globalPose): " << ex.what() << "\n";
      std::cout << "ExtrapolateException in transformPose(\"map\", robotPose, globalPose): " << ex.what();
      return;
    } catch(tf::ConnectivityException& ex) {
      std::cerr << "ConnectivityException in transformPose(\"map\", robotPose, globalPose): " << ex.what() << "\n";
      std::cout << "ConnectivityException in transformPose(\"map\", robotPose, globalPose): " << ex.what();
      return;
    }
    msg.x = globalPose.getOrigin().x();
    msg.y = globalPose.getOrigin().y();
    msg.z = globalPose.getOrigin().z();
    std::cout << msg.x << ", " << msg.y << ", " << msg.z;
    publish("groundtruthposition", msg);
  }
};

int main(int argc, char **argv) {
  ros::init(argc, argv);
  std::cout << "Starting...\n";
  GroundTruthTransform t;
  while (t.ok())
  {
    usleep(100000);
    std::cout << "Transform: ";
    t.speak();
    std::cout << std::endl;
  }
  
  return 0;
}