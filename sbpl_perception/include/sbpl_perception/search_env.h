/**
 * @file search_env.h
 * @brief Object recognition search environment
 * @author Venkatraman Narayanan
 * Carnegie Mellon University, 2015
 */

#ifndef _SBPL_PERCEPTION_SEARCH_ENV_H_
#define _SBPL_PERCEPTION_SEARCH_ENV_H_

#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>

#include <sbpl_perception/object_model.h>
#include <sbpl_perception/pcl_typedefs.h>
#include <sbpl_perception/vfh_pose_estimation.h>

#include <kinect_sim/simulation_io.hpp>
#include <kinect_sim/scene.h>
#include <kinect_sim/model.h>

#include <pcl/PolygonMesh.h>

#include <pcl/range_image/range_image_planar.h>

#include <pcl/registration/icp.h>
#include <pcl/registration/icp_nl.h>
#include <pcl/registration/transformation_estimation_2D.h>
#include <pcl/registration/transformation_estimation_lm.h>
#include <pcl/registration/transformation_estimation_svd.h>
#include <pcl/registration/warp_point_rigid_3d.h>

#include <pcl/search/organized.h>
#include <pcl/search/kdtree.h>

#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/visualization/range_image_visualizer.h>
#include <pcl/visualization/image_viewer.h>

#include <sbpl/headers.h>

#include <Eigen/Dense>
#include <unordered_map>

#define NUM_MODELS 4

inline double WrapAngle(double x) {
  x = fmod(x, 360);

  if (x < 0) {
    x += 360;
  }

  return x;
}

struct EnvParams {
  double table_height;
  Eigen::Isometry3d camera_pose;
  double x_min, x_max, y_min, y_max;
  double res, theta_res; // Resolution for x,y and theta
  int img_width, img_height;
  int goal_state_id, start_state_id;
  int num_objects; // This is the number of objects on the table
  int num_models; // This is the number of models available (can be more or less than number of objects on table
  unsigned short observed_max_range; // Max range in the observed point cloud
  unsigned short observed_min_range; // Min range in the observed point cloud
};

struct Pose {
  double x;
  double y;
  double theta;
  Pose() {
    x = 0.0;
    y = 0.0;
    theta = 0.0;
  }
  Pose(double x_val, double y_val, double theta_val) {
    x = x_val;
    y = y_val;
    theta = theta_val;
  }
  bool Equals(const Pose &p, bool symmetric) const {
    if (fabs(x - p.x) < 0.02 && fabs(y - p.y) < 0.02 &&
        (symmetric || fabs(WrapAngle(theta) - WrapAngle(p.theta)) < 0.1)) {  //M_PI/18
      return true;
    }

    return false;
  }
};

struct DiscPose {
  int x;
  int y;
  int theta;
  DiscPose(int x_val, int y_val, int theta_val) {
    x = x_val;
    y = y_val;
    theta = theta_val;
  }
};

struct State {
  std::vector<int> object_ids;
  std::vector<DiscPose> disc_object_poses;
  std::vector<Pose> object_poses;
};

struct StateProperties {
  unsigned short last_min_depth;
  unsigned short last_max_depth;
};

typedef struct {
  int source_ids[NUM_MODELS];
  int source_disc[NUM_MODELS*3];
  double source_pose[NUM_MODELS*3];
  int cand_ids[NUM_MODELS];
  int cand_disc[NUM_MODELS*3];
  double cand_pose[NUM_MODELS*3];
  int source_id;
  int cand_id;
  int valid;
} SendMsg;

typedef struct {
  int child_ids[NUM_MODELS];
  int child_disc[NUM_MODELS*3];
  double child_pose[NUM_MODELS*3];
  unsigned short last_min_depth;
  unsigned short last_max_depth;
  int cost;
  int valid;
} RecvMsg;

// class EnvObjectRecognition : public DiscreteSpaceInformation {
class EnvObjectRecognition : public EnvironmentMHA {
 public:
  EnvObjectRecognition(int size, int rank);
  EnvObjectRecognition();
  ~EnvObjectRecognition();
  int id;
  int num_proc;
  void LoadObjFiles(const std::vector<std::string> &model_files,
                    const std::vector<bool> model_symmetric);
  void SetScene();

  int ExpectedCountScatter(int *expected);
  void DataScatter(SendMsg* sendbuf, SendMsg* getbuf, int expected_count);
  int GetRecvdState(State *work_source_state, 
                    State *work_cand_succs,
                    int *work_source_id,
                    int *work_cand_id,
                    SendMsg* dummy,
                    int val);
  void SendbufPopulate(SendMsg *sendbuf, 
    State s, State p, int sid, int pid);
  void RecvbufPopulate(RecvMsg* sendbuf, 
                        State& s,
                        StateProperties& child_properties,
                        int cost);
  int GetRecvdResult(State *work_source_state, 
                                          StateProperties *child_properties_result,
                                          int *cost_result,
                                          RecvMsg* dummy,
                                          int tot);

  void DataGather(RecvMsg* recvbuf, 
                    RecvMsg* getresult, int expected_count);
  void DebugPrint(State s);
  void DebugPrintArray(SendMsg* s);
  void DebugPrintArrayRecv(RecvMsg* s);

  void WriteSimOutput(std::string fname_root);
  void PrintState(int state_id, std::string fname);
  void PrintState(State s, std::string fname);
  void PrintImage(std::string fname,
                  const std::vector<unsigned short> &depth_image);
  void TransformPolyMesh(const pcl::PolygonMesh::Ptr mesh_in,
                         pcl::PolygonMesh::Ptr mesh_out, Eigen::Matrix4f transform);
  void PreprocessModel(const pcl::PolygonMesh::Ptr mesh_in,
                       pcl::PolygonMesh::Ptr mesh_out);
  const float *GetDepthImage(State s, std::vector<unsigned short> *depth_image);

  pcl::simulation::SimExample::Ptr kinect_simulator_;

  void GenerateHalo(
    std::vector<Eigen::Isometry3d, Eigen::aligned_allocator<Eigen::Isometry3d>>
    &poses, Eigen::Vector3d focus_center, double halo_r, double halo_dz,
    int n_poses);

  /** Methods to set the observed depth image**/
  void SetObservation(std::vector<int> object_ids,
                      std::vector<Pose> poses);
  void SetObservation(int num_objects,
                      const std::vector<unsigned short> observed_depth_image,
                      const PointCloudPtr observed_organized_cloud);
  void SetObservation(int num_objects,
                      const unsigned short *observed_depth_image);
  void SetCameraPose(Eigen::Isometry3d camera_pose);
  void SetTableHeight(double height);
  void SetBounds(double x_min, double x_max, double y_min, double y_max);
  void PrecomputeHeuristics();

  double ComputeScore(const PointCloudPtr cloud);

  double GetICPAdjustedPose(const PointCloudPtr cloud_in, const Pose &pose_in,
                            PointCloudPtr cloud_out, Pose *pose_out);

  // Greedy ICP planner
  State ComputeGreedyICPPoses();
  State ComputeVFHPoses();

  // Heuristics
  int GetICPHeuristic(State s);
  int GetVFHHeuristic(State s);
  VFHPoseEstimator vfh_pose_estimator_;
  std::vector<Pose> vfh_poses_;
  std::vector<int> vfh_ids_;
  

  void GetSuccs(State source_state, std::vector<State> *succs,
                std::vector<int> *costs);
  bool IsGoalState(State state);
  int GetGoalStateID() {
    return env_params_.goal_state_id;  // Goal state has unique id
  }
  int GetStartStateID() {
    return env_params_.start_state_id;  // Goal state has unique id
  }

  bool StatesEqual(const State &s1,
                   const State
                   &s2); // Two states are equal if they have the same set of objects in the same poses
  bool StatesEqualOrdered(const State &s1,
                          const State
                          &s2); // Two states are 'ordered' equal if they have the same set of objects in the same poses, and placed in the same sequential order

  /**@brief State to State ID mapping**/
  int StateToStateID(State &s);
  /**@brief State ID to State mapping**/
  State StateIDToState(int state_id);



  void GetSuccs(int source_state_id, std::vector<int> *succ_ids,
                std::vector<int> *costs);

  void GetLazySuccs(int source_state_id, std::vector<int> *succ_ids,
                    std::vector<int> *costs,
                    std::vector<bool> *true_costs);
  void GetLazyPreds(int source_state_id, std::vector<int> *pred_ids,
                    std::vector<int> *costs,
                    std::vector<bool> *true_costs) {
    throw std::runtime_error("unimplement");
  }

  // For MHA
  void GetSuccs(int q_id, int source_state_id, std::vector<int> *succ_ids,
                std::vector<int> *costs) {
    printf("Expanding from %d\n", q_id);
    GetSuccs(source_state_id, succ_ids, costs);
  }

  void GetLazySuccs(int q_id, int source_state_id, std::vector<int> *succ_ids,
                    std::vector<int> *costs,
                    std::vector<bool> *true_costs) {
    throw std::runtime_error("don't use lazy for now...");
    printf("Expanding from %d\n", q_id);
    GetLazySuccs(source_state_id, succ_ids, costs, true_costs);
  }

  void GetLazyPreds(int q_id, int source_state_id, std::vector<int> *pred_ids,
                    std::vector<int> *costs,
                    std::vector<bool> *true_costs) {
    throw std::runtime_error("unimplement");
  }

  int GetGoalHeuristic(int state_id);
  int GetGoalHeuristic(int q_id, int state_id); // For MHA*
  int SizeofCreatedEnv() {
    return static_cast<int>(StateMap.size());
  }

  // Computes the cost for the parent-child edge. Returns the adjusted child state, where the pose
  // of the last added object is adjusted using ICP and the computed state properties.
  int GetTrueCost(const State source_state, const State child_state,
                  int parent_id, int child_id, State *adjusted_child_state,
                  StateProperties *state_properties);

  // Cost for newly rendered object. Input cloud must contain only newly rendered points.
  int GetTargetCost(const PointCloudPtr
                    partial_rendered_cloud);
  // Cost for points in observed cloud that can be computed based on the rendered cloud.
  int GetSourceCost(const PointCloudPtr full_rendered_cloud, const int parent_id,
                    const int child_id);
  // Returns true if parent is occluded by successor. Additionally returns min and max depth for newly rendered pixels
  // when occlusion-free.
  bool IsOccluded(const std::vector<unsigned short> &parent_depth_image,
                  const std::vector<unsigned short> &succ_depth_image,
                  std::vector<int> *new_pixel_indices, unsigned short *min_succ_depth,
                  unsigned short *max_succ_depth);

  bool IsValidPose(State s, int model_id, Pose p);


  void SetDebugOptions(bool image_debug);

  // Not needed
  bool InitializeEnv(const char *sEnvFile) {
    return false;
  };
  bool InitializeMDPCfg(MDPConfig *MDPCfg) {
    return true;
  };
  int  GetFromToHeuristic(int FromStateID, int ToStateID) {
    throw std::runtime_error("unimplement");
  };
  int  GetStartHeuristic(int stateID) {
    throw std::runtime_error("unimplement");
  };
  int  GetStartHeuristic(int q_id, int stateID) {
    throw std::runtime_error("unimplement");
  };
  void GetPreds(int TargetStateID, std::vector<int> *PredIDV,
                std::vector<int> *CostV) {};
  void SetAllActionsandAllOutcomes(CMDPSTATE *state) {};
  void SetAllPreds(CMDPSTATE *state) {};
  void PrintState(int stateID, bool bVerbose, FILE *fOut = NULL) {};
  void PrintEnv_Config(FILE *fOut) {};


 private:

  std::vector<ObjectModel> obj_models_;
  std::vector<std::string> model_files_;
  pcl::simulation::Scene::Ptr scene_;

  EnvParams env_params_;
  /**@brief Mapping from State to State ID**/
  std::unordered_map<int, State> StateMap;
  std::unordered_map<int, int> HeuristicMap;
  std::unordered_map<int, std::vector<int>> succ_cache;
  std::unordered_map<int, std::vector<int>> cost_cache;
  std::unordered_map<int, unsigned short> minz_map_;
  std::unordered_map<int, unsigned short> maxz_map_;
  std::unordered_map<int, std::vector<int>>
                                         counted_pixels_map_; // Keep track of the pixels we have accounted for in cost computation for a given state

  // pcl::search::OrganizedNeighbor<PointT>::Ptr knn;
  pcl::search::KdTree<PointT>::Ptr knn;


  std::vector<unsigned short> observed_depth_image_;
  PointCloudPtr observed_cloud_, downsampled_observed_cloud_,
                observed_organized_cloud_;
  pcl::RangeImagePlanar empty_range_image_;

  State start_state_, goal_state_;

  bool image_debug_;

  Eigen::Matrix4f gl_inverse_transform_;
  Eigen::Isometry3d cam_to_world_;

  std::vector<int> sorted_greedy_icp_ids_;
  std::vector<double> sorted_greedy_icp_scores_;

};

#endif /** _SBPL_PERCEPTION_SEARCH_ENV_H **/

















