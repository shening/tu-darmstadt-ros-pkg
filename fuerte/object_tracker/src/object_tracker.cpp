#include "object_tracker.h"

#include <hector_nav_msgs/GetDistanceToObstacle.h>
#include <worldmodel_msgs/VerifyObject.h>
#include <worldmodel_msgs/VerifyPercept.h>

#include <Eigen/Geometry>
#include <math.h>

#include "Object.h"

#include <boost/algorithm/string.hpp>

namespace object_tracker {

ObjectTracker::ObjectTracker()
{
  ros::NodeHandle priv_nh("~");
  priv_nh.param("project_objects", _project_objects, false);
  priv_nh.param("frame_id", _frame_id, std::string("map"));
  priv_nh.param("worldmodel_ns", _worldmodel_ns, std::string("worldmodel"));
  priv_nh.param("default_distance", _default_distance, 1.0);
  priv_nh.param("distance_variance", _distance_variance, pow(1.0, 2));
  priv_nh.param("angle_variance", _angle_variance, pow(10.0 * M_PI / 180.0, 2));
  priv_nh.param("min_height", _min_height, -999.9);
  priv_nh.param("max_height", _max_height, 999.9);
  priv_nh.param("pending_support", _pending_support, 0.0);
  priv_nh.param("pending_time", _pending_time, 0.0);
  priv_nh.param("active_support", _active_support, 0.0);
  priv_nh.param("active_time", _active_time, 0.0);
  priv_nh.param("ageing_threshold", _ageing_threshold, 1.0);

  ros::NodeHandle worldmodel(_worldmodel_ns);
  imagePerceptSubscriber = worldmodel.subscribe("image_percept", 10, &ObjectTracker::imagePerceptCb, this);
  posePerceptSubscriber = worldmodel.subscribe("pose_percept", 10, &ObjectTracker::posePerceptCb, this);
  objectAgeingSubscriber = worldmodel.subscribe("object_ageing", 10, &ObjectTracker::objectAgeingCb, this);
  modelPublisher = worldmodel.advertise<worldmodel_msgs::ObjectModel>("objects", 10, false);
  modelUpdatePublisher = worldmodel.advertise<worldmodel_msgs::Object>("object", 10, false);

  Object::setNamespace(_worldmodel_ns);
  drawings.setNamespace(_worldmodel_ns);

  sysCommandSubscriber = nh.subscribe("syscommand", 10, &ObjectTracker::sysCommandCb, this);
  poseDebugPublisher = priv_nh.advertise<geometry_msgs::PoseStamped>("pose", 10, false);
  pointDebugPublisher = priv_nh.advertise<geometry_msgs::PointStamped>("point", 10, false);

  XmlRpc::XmlRpcValue verification_services;
  if (priv_nh.getParam("verification_services", verification_services) && verification_services.getType() == XmlRpc::XmlRpcValue::TypeArray) {
    for(int i = 0; i < verification_services.size(); ++i) {
      XmlRpc::XmlRpcValue item = verification_services[i];
      if (!item.hasMember("service")) {
        ROS_ERROR("Verification service %d could not be intialized: unknown service name", i);
        continue;
      }
      if (!item.hasMember("type")) {
        ROS_ERROR("Verification service %d could not be intialized: unknown service type", i);
        continue;
      }

      ros::ServiceClient client;
      if (item["type"] == "object") {
        client = nh.serviceClient<worldmodel_msgs::VerifyObject>(item["service"]);
      } else if (item["type"] == "percept") {
        client = nh.serviceClient<worldmodel_msgs::VerifyPercept>(item["service"]);
      }

      if (!client.isValid()) continue;
      if (!client.exists()) {
        if (!item.hasMember("required") || !item["required"]) {
          ROS_WARN("Verification service %s is not (yet) there...", client.getService().c_str());
        } else {
          ROS_WARN("Required verification service %s is not available... waiting...", client.getService().c_str());
          while(ros::ok() && !client.waitForExistence(ros::Duration(1.0)));
        }
      }

      if (item.hasMember("class_id")) {
        verificationServices[item["type"]][item["class_id"]].push_back(std::make_pair(client, item));
        ROS_INFO("Using %s verification service %s for objects of class %s", std::string(item["type"]).c_str(), client.getService().c_str(), std::string(item["class_id"]).c_str());
      } else {
        verificationServices[item["type"]]["*"].push_back(std::make_pair(client, item));
        ROS_INFO("Using %s verification service %s", std::string(item["type"]).c_str(), client.getService().c_str());
      }
    }
  }

  distanceToObstacle = nh.serviceClient<hector_nav_msgs::GetDistanceToObstacle>("get_distance_to_obstacle");
  if (_project_objects && !distanceToObstacle.waitForExistence(ros::Duration(5.0))) {
    ROS_WARN("_project_objects is true, but GetDistanceToObstacle service is not (yet) available");
  }

  setObjectState = worldmodel.advertiseService("set_object_state", &ObjectTracker::setObjectStateCb, this);
  setObjectName  = worldmodel.advertiseService("set_object_name", &ObjectTracker::setObjectNameCb, this);
  addObject = worldmodel.advertiseService("add_object", &ObjectTracker::addObjectCb, this);
  getObjectModel = worldmodel.advertiseService("get_object_model", &ObjectTracker::getObjectModelCb, this);
}

ObjectTracker::~ObjectTracker()
{}

void ObjectTracker::sysCommandCb(const std_msgs::StringConstPtr &sysCommand)
{
  if (sysCommand->data == "reset") {
    ROS_INFO("Resetting object model.");
    model.reset();
    drawings.reset();
  }
}

void ObjectTracker::imagePerceptCb(const worldmodel_msgs::ImagePerceptConstPtr &percept)
{
  worldmodel_msgs::PosePerceptPtr posePercept(new worldmodel_msgs::PosePercept);
  tf::Pose pose;

  ROS_DEBUG("Incoming image percept with image coordinates [%f,%f] in frame %s", percept->x, percept->y, percept->header.frame_id.c_str());

  posePercept->header = percept->header;
  posePercept->info = percept->info;

  // retrieve distance information
  float distance = percept->distance > 0.0 ? percept->distance : _default_distance;

  // retrieve camera model from either the cache or from CameraInfo given in the percept
  CameraModelPtr cameraModel;
  if (cameraModels.count(percept->header.frame_id) == 0) {
    cameraModel.reset(new image_geometry::PinholeCameraModel());
    if (!cameraModel->fromCameraInfo(percept->camera_info)) {
      ROS_ERROR("Could not initialize camera model from CameraInfo given in the percept");
      return;
    }
    cameraModels[percept->header.frame_id] = cameraModel;
  } else {
    cameraModel = cameraModels[percept->header.frame_id];
  }

  // transform Point using the camera mode
  cv::Point2d rectified = cameraModel->rectifyPoint(cv::Point2d(percept->x, percept->y));
  cv::Point3d direction_cv = cameraModel->projectPixelTo3dRay(rectified);
//  pose.setOrigin(tf::Point(direction_cv.z, -direction_cv.x, -direction_cv.y).normalized() * distance);
//  tf::Quaternion direction(atan2(-direction_cv.x, direction_cv.z), atan2(direction_cv.y, sqrt(direction_cv.z*direction_cv.z + direction_cv.x*direction_cv.x)), 0.0);
  pose.setOrigin(tf::Point(direction_cv.x, direction_cv.y, direction_cv.z).normalized() * distance);
  tf::Quaternion direction;
  direction.setEuler(atan2(direction_cv.x, direction_cv.z), atan2(-direction_cv.y, sqrt(direction_cv.z*direction_cv.z + direction_cv.x*direction_cv.x)), 0.0);
  direction = direction * tf::Quaternion(0.5, -0.5, 0.5, 0.5);
  pose.getBasis().setRotation(direction);

  ROS_DEBUG("--> Rectified image coordinates: [%f,%f]", rectified.x, rectified.y);
  ROS_DEBUG("--> Projected 3D ray (OpenCV):   [%f,%f,%f]", direction_cv.x, direction_cv.y, direction_cv.z);
  ROS_DEBUG("--> Projected 3D ray (tf):       [%f,%f,%f]", pose.getOrigin().x(), pose.getOrigin().y(),pose.getOrigin().z());

  if (percept->distance == 0.0 && _project_objects) {
    hector_nav_msgs::GetDistanceToObstacle::Request request;
    hector_nav_msgs::GetDistanceToObstacle::Response response;

    // project image percept to the next obstacle
    request.point.header = percept->header;
    tf::pointTFToMsg(pose.getOrigin(), request.point.point);
    if (distanceToObstacle.call(request, response)) {
      if (response.distance > 0.0) {
        // distance = std::max(response.distance - 0.1f, 0.0f);
        distance = std::max(response.distance, 0.0f);
        pose.setOrigin(pose.getOrigin().normalized() * distance);
        ROS_DEBUG("Projected percept to a distance of %.1f m", distance);
      } else {
        ROS_WARN("Ignoring percept due to unknown or infinite distance: service %s returned %f", distanceToObstacle.getService().c_str(), response.distance);
        return;
      }
    } else {
      ROS_WARN("Ignoring percept due to unknown or infinite distance: service %s is not available", distanceToObstacle.getService().c_str());
      return;
    }
  }

  // set variance
  Eigen::Matrix3f covariance(Eigen::Matrix3f::Zero());
  covariance(0,0) = std::max(distance*distance, 1.0f) * tan(_angle_variance);
  covariance(1,1) = covariance(0,0);
  covariance(2,2) = _distance_variance;

  // rotate covariance matrix depending on the position in the image
  Eigen::Quaterniond eigen_rotation(direction.w(), direction.x(), direction.y(), direction.z());
  Eigen::Matrix3f rotation_camera_object(eigen_rotation.toRotationMatrix().cast<float>());
  covariance = rotation_camera_object * covariance * rotation_camera_object.transpose();

  // fill posePercept
  tf::poseTFToMsg(pose, posePercept->pose.pose);
  // tf::quaternionTFToMsg(direction, posePercept->pose.pose.orientation);
  posePercept->pose.covariance[0]  = covariance(0,0);
  posePercept->pose.covariance[1]  = covariance(0,1);
  posePercept->pose.covariance[2]  = covariance(0,2);
  posePercept->pose.covariance[6]  = covariance(1,0);
  posePercept->pose.covariance[7]  = covariance(1,1);
  posePercept->pose.covariance[8]  = covariance(1,2);
  posePercept->pose.covariance[12] = covariance(2,0);
  posePercept->pose.covariance[13] = covariance(2,1);
  posePercept->pose.covariance[14] = covariance(2,2);

  // forward to posePercept callback
  posePerceptCb(posePercept);
}

void ObjectTracker::posePerceptCb(const worldmodel_msgs::PosePerceptConstPtr &percept)
{
  // publish pose in source frame for debugging purposes
  if (poseDebugPublisher.getNumSubscribers() > 0) {
    geometry_msgs::PoseStamped pose;
    pose.pose = percept->pose.pose;
    pose.header = percept->header;
    poseDebugPublisher.publish(pose);
  }

  // call percept verification
  float support_added_by_percept_verification = 0.0;
  if (verificationServices.count("percept") > 0) {
    worldmodel_msgs::VerifyPercept::Request request;
    worldmodel_msgs::VerifyPercept::Response response;

    request.percept = *percept;

    std::vector<VerificationService> services(verificationServices["percept"]["*"]);
    if (!percept->info.class_id.empty()) {
      services.insert(services.end(), verificationServices["percept"][percept->info.class_id].begin(), verificationServices["percept"][percept->info.class_id].end());
    }

    for(std::vector<VerificationService>::iterator it = services.begin(); it != services.end(); ++it) {
      if (it->second.hasMember("ignore") && it->second["ignore"]) {
        ROS_DEBUG("Calling service %s for percept of class '%s'', but ignoring its answer...", it->first.getService().c_str(), percept->info.class_id.c_str());
        it->first.call(request, response);

      } else if (it->first.call(request, response)) {
        if (response.response == response.DISCARD) {
          ROS_DEBUG("Discarded percept of class '%s' due to DISCARD message from service %s", percept->info.class_id.c_str(), it->first.getService().c_str());
          return;
        }
        if (response.response == response.CONFIRM) {
          ROS_DEBUG("We got a CONFIRMation for percept of class '%s' from service %s!", percept->info.class_id.c_str(), it->first.getService().c_str());
          support_added_by_percept_verification = 100.0;
        }
        if (response.response == response.UNKNOWN) {
          ROS_DEBUG("Verification service %s cannot help us with percept of class %s at the moment :-(", it->first.getService().c_str(), percept->info.class_id.c_str());
        }
      } else if (it->second.hasMember("required") && it->second["required"]) {
        ROS_DEBUG("Discarded percept of class '%s' as required service %s is not available", percept->info.class_id.c_str(), it->first.getService().c_str());
        return;
      }
    }
  }

  // convert pose in tf
  tf::Pose pose;
  tf::poseMsgToTF(percept->pose.pose, pose);

  // retrieve distance information
//  float distance = pose.getOrigin().length();
//  if (_project_objects) {
//    hector_nav_msgs::GetDistanceToObstacle::Request request;
//    hector_nav_msgs::GetDistanceToObstacle::Response response;

//    // project image percept to the next obstacle
//    request.point.header = percept->header;
//    tf::pointTFToMsg(pose.getOrigin(), request.point.point);
//    if (distanceToObstacle.call(request, response) && response.distance > 0.0) {
//      // distance = std::max(response.distance - 0.1f, 0.0f);
//      distance = std::max(response.distance, 0.0f);
//      pose.setOrigin(pose.getOrigin().normalized() * distance);
//      ROS_DEBUG("Projected percept to a distance of %.1f m", distance);
//    } else {
//      ROS_DEBUG("Ignoring percept due to unknown or infinite distance");
//      return;
//    }
//  }

  // extract variance matrix
  Eigen::Matrix<float,6,6> temp;
  for(unsigned int i = 0; i < 36; ++i) temp(i) = percept->pose.covariance[i];
  Eigen::Matrix3f covariance = temp.block<3,3>(0,0);

  // if no variance is given, set variance to default
  if (covariance.isZero()) {
    covariance(0,0) = _distance_variance;
    covariance(1,1) = _distance_variance;
    covariance(2,2) = _distance_variance;
  }

  // project percept coordinates to map frame
  tf::StampedTransform cameraTransform;
  if (!_frame_id.empty() && tf.resolve(percept->header.frame_id) != tf.resolve(_frame_id)) {
    ROS_DEBUG("Transforming percept from %s frame to %s frame", percept->header.frame_id.c_str(), _frame_id.c_str());

    // retrieve camera transformation from tf
    try {
      tf.waitForTransform(_frame_id, percept->header.frame_id, percept->header.stamp, ros::Duration(1.0));
      tf.lookupTransform(_frame_id, percept->header.frame_id, percept->header.stamp, cameraTransform);
    } catch (tf::TransformException ex) {
      ROS_ERROR("%s", ex.what());
      return;
    }

    pose = cameraTransform * pose;

    // rotate covariance matrix to map coordinates
    Eigen::Quaterniond eigen_rotation(cameraTransform.getRotation().w(), cameraTransform.getRotation().x(), cameraTransform.getRotation().y(), cameraTransform.getRotation().z());
    Eigen::Matrix3f rotation_map_camera(eigen_rotation.toRotationMatrix().cast<float>());
    covariance = rotation_map_camera * covariance * rotation_map_camera.transpose();
  }
  Eigen::Vector3f position(pose.getOrigin().x(), pose.getOrigin().y(), pose.getOrigin().z());

  // check height
  float relative_height = pose.getOrigin().z() - cameraTransform.getOrigin().z();
  if (relative_height < _min_height || relative_height > _max_height) {
    ROS_INFO("Discarding %s percept with height %f", percept->info.class_id.c_str(), relative_height);
    return;
  }

  // fix height (assume camera is always at 0.475m)
  // pose.setOrigin(tf::Point(pose.getOrigin().x(), pose.getOrigin().y(), pose.getOrigin().z() - cameraTransform.getOrigin().z() + 0.475f));

  // calculate observation support
  float support = 0.0;
  if (!percept->info.object_id.empty()) {
    support = percept->info.object_support;
  } else if (!percept->info.class_id.empty()) {
    support = percept->info.class_support + support_added_by_percept_verification;
  }

  if (support == 0.0) {
    ROS_WARN("Ignoring percept with support == 0.0");
    return;
  }

  // lock model
  model.lock();

  // find correspondence
  ObjectPtr object;
  float min_distance = 1.0f;
  if (percept->info.object_id.empty()) {
    for(ObjectModel::iterator it = model.begin(); it != model.end(); ++it) {
      ObjectPtr x = *it;
      if (!percept->info.class_id.empty() && percept->info.class_id != x->getClassId()) continue;
      Eigen::Vector3f diff = x->getPosition() - position;
      float distance = (diff.transpose() * (x->getCovariance() + covariance).inverse() * diff)[0];
      if (distance < min_distance) {
        object = x;
        min_distance = distance;
      }
    }
  } else {
    object = model.getObject(percept->info.object_id);
  }

  if (object && object->getState() < 0) {
    ROS_DEBUG("Percept was associated to object %s, which has a fixed state", object->getObjectId().c_str());
    model.unlock();
    return;
  }

  // create new object
  if (!object) {
    object = model.add(percept->info.class_id, percept->info.object_id);

    object->setPosition(position);
    object->setCovariance(covariance);
    object->setSupport(support);

    ROS_INFO("Found new object %s of class %s at (%f,%f)!", object->getObjectId().c_str(), object->getClassId().c_str(), position.x(), position.y());

  // or update existing object
  } else if (support > 0.0) {
    //object->update(position, covariance, support);
    object->intersect(position, covariance, support);

  // or simply decrease support
  } else {
    object->addSupport(support);
  }

  // update object state
  if (object->getState() == worldmodel_msgs::ObjectState::UNKNOWN &&  _pending_support > 0) {
    if (object->getSupport() >= _pending_support && (percept->header.stamp - object->getHeader().stamp).toSec() >= _pending_time) {
      ROS_INFO("Setting object state for %s to PENDING", object->getObjectId().c_str());
      object->setState(worldmodel_msgs::ObjectState::PENDING);
    }
  }
  if (object->getState() == worldmodel_msgs::ObjectState::PENDING &&  _active_support > 0) {
    if (object->getSupport() >= _active_support && (percept->header.stamp - object->getHeader().stamp).toSec() >= _active_time) {
      ROS_INFO("Setting object state for %s to ACTIVE", object->getObjectId().c_str());
      object->setState(worldmodel_msgs::ObjectState::ACTIVE);
    }
  }

  // set object orientation
  geometry_msgs::Quaternion object_orientation;
  tf::quaternionTFToMsg(pose.getRotation(), object_orientation);
  object->setOrientation(object_orientation);

  // update object header
  std_msgs::Header header = percept->header;
  header.frame_id = _frame_id;
  object->setHeader(header);

  // update object name
  if (!percept->info.name.empty()) object->setName(percept->info.name);

  // unlock model
  model.unlock();

  // call object verification
  if (verificationServices.count("object") > 0) {
    worldmodel_msgs::VerifyObject::Request request;
    worldmodel_msgs::VerifyObject::Response response;

    request.object = object->getObjectMessage();

    std::vector<VerificationService> services(verificationServices["object"]["*"]);
    if (!object->getClassId().empty()) {
      services.insert(services.end(), verificationServices["object"][object->getClassId()].begin(), verificationServices["object"][object->getClassId()].end());
    }

    for(std::vector<VerificationService>::iterator it = services.begin(); it != services.end(); ++it) {
      if (it->second.hasMember("ignore") && it->second["ignore"]) {
        ROS_DEBUG("Calling service %s for object %s, but ignoring its answer...", it->first.getService().c_str(), object->getObjectId().c_str());
        it->first.call(request, response);

      } else if (it->first.call(request, response)) {
        if (response.response == response.DISCARD) {
          ROS_DEBUG("Discarded object %s due to DISCARD message from service %s", object->getObjectId().c_str(), it->first.getService().c_str());
          object->setState(worldmodel_msgs::ObjectState::DISCARDED);
        }
        if (response.response == response.CONFIRM) {
          ROS_DEBUG("We got a CONFIRMation for object %s from service %s!", object->getObjectId().c_str(), it->first.getService().c_str());
          object->addSupport(100.0);
        }
        if (response.response == response.UNKNOWN) {
          ROS_DEBUG("Verification service %s cannot help us with object %s at the moment :-(", it->first.getService().c_str(), object->getObjectId().c_str());
        }
      } else if (it->second.hasMember("required") && it->second["required"]) {
        ROS_DEBUG("Discarded object %s as required service %s is not available", object->getObjectId().c_str(), it->first.getService().c_str());
        object->setState(worldmodel_msgs::ObjectState::DISCARDED);
      }
    }
  }

  // publish point in target frame for debugging purposes
  if (pointDebugPublisher.getNumSubscribers() > 0) {
    geometry_msgs::PointStamped point;
    point.point = object->getPose().position;
    point.header = object->getHeader();
    pointDebugPublisher.publish(point);
  }

  modelUpdatePublisher.publish(object->getObjectMessage());
  publishModel();
}

void ObjectTracker::objectAgeingCb(const std_msgs::Float32ConstPtr &ageing) {
  ROS_DEBUG("ageing of all objects by %f", ageing->data);

  // lock model
  model.lock();

  ObjectModel::ObjectList objects = model.getObjects();
  
  for(ObjectModel::iterator it = objects.begin(); it != objects.end();) {
    ObjectModel::ObjectPtr object = *it;

    // update support
    object->setSupport(object->getSupport() - ageing->data);

    // remove the object if the support is to low
    if (object->getSupport() < _ageing_threshold) {
      ROS_INFO("remove object %s with support %f", object->getObjectId().c_str(), object->getSupport());
      it = objects.erase(it);
      model.remove(object);
    } else {
      it++;
    }
  }

  // unlock model
  model.unlock();
  publishModel();
}

bool ObjectTracker::setObjectStateCb(worldmodel_msgs::SetObjectState::Request& request, worldmodel_msgs::SetObjectState::Response& response) {
  model.lock();

  ObjectPtr object = model.getObject(request.object_id);
  if (!object) {
    model.unlock();
    return false;
  }

  object->setState(request.new_state.state);
  modelUpdatePublisher.publish(object->getObjectMessage());

  model.unlock();
  publishModel();
  return true;
}

bool ObjectTracker::setObjectNameCb(worldmodel_msgs::SetObjectName::Request& request, worldmodel_msgs::SetObjectName::Response& response) {
  model.lock();

  ObjectPtr object = model.getObject(request.object_id);
  if (!object) {
    model.unlock();
    return false;
  }

  object->setName(request.name);
  modelUpdatePublisher.publish(object->getObjectMessage());

  model.unlock();
  publishModel();
  return true;
}

bool ObjectTracker::addObjectCb(worldmodel_msgs::AddObject::Request& request, worldmodel_msgs::AddObject::Response& response) {
  ObjectPtr object;
  bool newObject = false;

  // check if object already exist
  if (!request.object.info.object_id.empty()) {
    ROS_INFO("add_object service called for known %s object %s in frame %s", request.object.info.class_id.c_str(), request.object.info.object_id.c_str(), request.object.header.frame_id.c_str());
    object = model.getObject(request.object.info.object_id);
  } else {
    ROS_INFO("add_object service called for new %s object in frame %s", request.object.info.class_id.c_str(), request.object.header.frame_id.c_str());
  }

  // create a new object if object does not exist
  if (!object) {
    object.reset(new Object(request.object.info.class_id, request.object.info.object_id));
    newObject = true;
  }

  std_msgs::Header header = request.object.header;
  if (header.stamp.isZero()) header.stamp = ros::Time::now();

  geometry_msgs::PoseWithCovariance pose;
  if (request.map_to_next_obstacle) {
    pose.covariance = request.object.pose.covariance;
    if (!mapToNextObstacle(request.object.pose.pose, header, pose.pose)) {
      return false;
    }
  } else {
    pose = request.object.pose;
  }

  // extract variance matrix
  Eigen::Matrix<float,6,6> temp;
  for(unsigned int i = 0; i < 36; ++i) temp(i) = pose.covariance[i];
  Eigen::Matrix3f covariance = temp.block<3,3>(0,0);

  // if no variance is given, set variance to default
  if (covariance.isZero()) {
    pose.covariance[0] = 1.0;
    pose.covariance[7] = 1.0;
    pose.covariance[14] = 1.0;
  }

  if (!transformPose(pose, pose, header)) return false;

  model.lock();

  object->setHeader(header);
  object->setPose(pose);
  object->setState(request.object.state.state);
  object->setSupport(request.object.info.support);

  if (newObject) model.add(object);
  response.object = object->getObjectMessage();
  modelUpdatePublisher.publish(response.object);

  model.unlock();

  publishModel();
  return true;
}

bool ObjectTracker::getObjectModelCb(worldmodel_msgs::GetObjectModel::Request& request, worldmodel_msgs::GetObjectModel::Response& response) {
  response.model = *(model.getObjectModelMessage());
  return true;
}

bool ObjectTracker::mapToNextObstacle(const geometry_msgs::Pose& source, const std_msgs::Header &header, geometry_msgs::Pose &mapped) {
  if (!distanceToObstacle.exists()) return false;

  // retrieve distance information
  float distance = _default_distance;
  hector_nav_msgs::GetDistanceToObstacle::Request request;
  hector_nav_msgs::GetDistanceToObstacle::Response response;

  // project image percept to the next obstacle
  request.point.header = header;
  request.point.point = source.position;
  // tf::pointTFToMsg(cameraTransform.getOrigin(), request.pose.pose.position);
  // tf::Quaternion direction_quaternion = tf::Quaternion(atan(direction.y/direction.x), atan(direction.z/direction.x), 0.0);
  // direction_quaternion *= cameraTransform.getRotation();
  // tf::quaternionTFToMsg(direction_quaternion, request.pose.pose.orientation);
  if (distanceToObstacle.call(request, response) && response.distance > 0.0) {
    // distance = std::max(response.distance - 0.1f, 0.0f);
    distance = std::max(response.distance, 0.0f);
  } else {
    ROS_DEBUG("Could not map object to next obstacle due to unknown or infinite distance");
    return false;
  }

  tf::Pose sourceTF;
  tf::poseMsgToTF(source, sourceTF);
  sourceTF.setOrigin(sourceTF.getOrigin().normalized() * distance);
  tf::poseTFToMsg(sourceTF, mapped);

  return true;
}

bool ObjectTracker::transformPose(const geometry_msgs::Pose& from, geometry_msgs::Pose &to, std_msgs::Header &header, tf::StampedTransform *transform_ptr) {
  // retrieve transformation from tf
  tf::StampedTransform transform;
  try {
    tf.waitForTransform(_frame_id, header.frame_id, header.stamp, ros::Duration(1.0));
    tf.lookupTransform(_frame_id, header.frame_id, header.stamp, transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return false;
  }

  tf::Pose tfPose;
  tf::poseMsgToTF(from, tfPose);
  tfPose = transform * tfPose;
  tf::poseTFToMsg(tfPose, to);

  header.frame_id = _frame_id;
  if (transform_ptr) *transform_ptr = transform;

  return true;
}

bool ObjectTracker::transformPose(const geometry_msgs::PoseWithCovariance& from, geometry_msgs::PoseWithCovariance &to, std_msgs::Header &header) {
  tf::StampedTransform transform;

  if (!transformPose(from.pose, to.pose, header, &transform)) return false;

  // TODO
  // rotate covariance matrix

  return true;
}

void ObjectTracker::publishModel() {
  // Publish all model data on topic /objects
  modelPublisher.publish(model.getObjectModelMessage());

  // Visualize victims and covariance in rviz
  visualization_msgs::MarkerArray markers;
  model.getVisualization(markers);
//  drawings.setTime(ros::Time::now());
//  model.lock();
//  for(ObjectModel::iterator it = model.begin(); it != model.end(); ++it) {
//    ObjectPtr object = *it;
//    drawings.addMarkers(object->getVisualization());
//    drawings.setColor(1.0, 0.0, 0.0, drawings.markerArray.markers.back().color.a);
//    drawings.drawCovariance(Eigen::Vector2f(object->getPosition().x(), object->getPosition().y()), object->getCovariance().block<2,2>(0,0));
//  }
//  model.unlock();
  drawings.addMarkers(markers);
  drawings.sendAndResetData();
}

} // namespace object_tracker


int main(int argc, char **argv)
{
  ros::init(argc, argv, ROS_PACKAGE_NAME);

  object_tracker::ObjectTracker tracker;
  ros::spin();

  exit(0);
}
