#include <hector_elevation_msgs/ElevationMapMetaData.h>
#include <hector_elevation_msgs/ElevationGrid.h>

#include <std_msgs/String.h>
#include <nav_msgs/OccupancyGrid.h>
#include <nav_msgs/MapMetaData.h>

#include <ros/ros.h>

#include <opencv/cv.h>
#include <opencv/highgui.h>

#include <cv_bridge/cv_bridge.h>

#include <vector>

#include <hector_map_tools/HectorMapTools.h>

#include <tf/transform_listener.h>

#include <dynamic_reconfigure/server.h>
#include <hector_costmap/CostMapCalculationConfig.h>



class CostMapCalculation{

public:
    /// Default constructor
    CostMapCalculation();

    /// Default deconstructor
    ~CostMapCalculation();

    /// updateMapParamsCallback updates the map meta information if someone has changed it
    /**
    * \param [in] map_meta_data map meta information like grid resolution or origin
    */
    void updateMapParamsCallback(const nav_msgs::MapMetaData& map_meta_data);

    /// callbackElevationMap get called if a new elevation map is avaible
    /**
    * \param [in] elevation_map_msg stores elevation map data as a 2.5D grid
    */
    void callbackElevationMap(const hector_elevation_msgs::ElevationGridConstPtr& elevation_map_msg);

    /// callbackGridMap get called if a new 2D grid map is avaible
    /**
    * \param [in] grid_map_msg stores 2D grid map data
    */
    void callbackGridMap(const nav_msgs::OccupancyGridConstPtr& grid_map_msg);

    /// sysMessageCallback This function listen to system messages
    /**
    * \param [in] string parameter contains system messages, like "reset"
    */
    void sysMessageCallback(const std_msgs::String& string);

    /// dynRecParamCallback This function get called if new parameters has been set with the dynamic reconfigure dialog
    /**
    * \param [in] config contains current parameters
    * \param [in] level is unused
    */
    void dynRecParamCallback(hector_costmap::CostMapCalculationConfig &config, uint32_t level);

    /// timerCallback publishes periodically a new 2D cost map
    /**
    * \param [in] event is unused
    */
    void timerCallback(const ros::TimerEvent& event);

private:
    ros::NodeHandle nHandle;

    ros::Publisher pub_cost_map;

    ros::Subscriber sub_elevation_map;
    ros::Subscriber sub_grid_map;
    ros::Subscriber sub_sysMessage;
    ros::Subscriber sub_map_info;

    dynamic_reconfigure::Server<hector_costmap::CostMapCalculationConfig> dyn_rec_server_;

    tf::TransformListener listener;

    ros::Timer timer;

    HectorMapTools::CoordinateTransformer<float> world_map_transform;

    double update_radius_world, initial_free_cells_radius;
    double grid_res_z;
    int elevation_zero_level;
    double max_delta_elevation;
    bool use_elevation_map, use_grid_map, received_grid_map, received_elevation_map, allow_kinect_to_clear_occupied_cells;
    int max_clear_size;
    double costmap_pub_freq;

    nav_msgs::OccupancyGridConstPtr grid_map_msg_;

    nav_msgs::OccupancyGrid cost_map, elevation_cost_map;

    cv::Mat grid_map_, elevation_map_, elevation_map_filtered,elevation_cost_map_;

    std::string cost_map_topic, elevation_map_topic, grid_map_topic, paramSysMsgTopic;
    std::string map_frame_id,local_map_frame_id;

    Eigen::Vector2i min_index, max_index;

    /// This function fuses the elevation and grid map und calculates the 2d cost map
    /**
    * \param [in] map_level set the method to calculate the 2d cost map
    * \retval true if everything went fine, otherwise false
    */
    bool calculateCostMap(char map_level);

    /// Computes the indices for the bounding box thats get updated
    /**
    * \param [in] time Current time is needed
    * \param [in] update_radius determines the size of the bounding box in [m]
    * \retval true if everything went fine, otherwise false
    */
    bool computeWindowIndices(ros::Time time,double update_radius);
};

