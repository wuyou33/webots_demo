/************************************************* 
Copyright:Webots Demo
Author: 锡城筱凯
Date:2021-06-30 
Blog：https://blog.csdn.net/xiaokai1999
Description:Webots Demo 机器人在cartographer建图算法下专用启动程序
**************************************************/  
#include <signal.h>
#include <std_msgs/String.h>
#include <geometry_msgs/PointStamped.h>
#include <sensor_msgs/Imu.h>
#include <nav_msgs/Odometry.h>
#include <rosgraph_msgs/Clock.h>
#include <tf/transform_broadcaster.h>
#include "ros/ros.h"

#include <webots_ros/set_float.h>
#include <webots_ros/set_int.h>
#include <webots_ros/Int32Stamped.h>
 
using namespace std;
#define TIME_STEP 32                        // 时钟
#define NMOTORS 2                           // 电机数量
#define MAX_SPEED 2.0                       // 电机最大速度
#define ROBOT_NAME "robot/"                 // ROBOT名称 

ros::NodeHandle *n;

static int controllerCount;
static vector<string> controllerList; 

ros::ServiceClient timeStepClient;          // 时钟通讯service客户端
webots_ros::set_int timeStepSrv;            // 时钟服务数据

ros::Publisher odompub;                     // 发布odom topic

double GPSvalues[4];                        // GPS数据
int gps_flag=1;                             // 记录第一次gps数据用的标志位
double Inertialvalues[4];                   // IMU数据
double liner_speed=0,angular_speed=0;       // 暂存的线速度和角速度,

/*******************************************************
* Function name ：controllerNameCallback
* Description   ：控制器名回调函数，获取当前ROS存在的机器人控制器
* Parameter     ：
        @name   控制器名
* Return        ：无
**********************************************************/
void controllerNameCallback(const std_msgs::String::ConstPtr &name) { 
    controllerCount++; 
    controllerList.push_back(name->data);//将控制器名加入到列表中
    ROS_INFO("Controller #%d: %s.", controllerCount, controllerList.back().c_str());
}

/*******************************************************
* Function name ：quit
* Description   ：退出函数
* Parameter     ：
        @sig   退出信号
* Return        ：无
**********************************************************/
void quit(int sig) {
    ROS_INFO("User stopped the '/robot' node.");
    timeStepSrv.request.value = 0; 
    timeStepClient.call(timeStepSrv); 
    ros::shutdown();
    exit(0);
}

/*******************************************************
* Function name ：broadcastTransform
* Description   ：机器人TF坐标系转换发布函数
* Parameter     ：无
* Return        ：无
**********************************************************/
void broadcastTransform(){
    static tf::TransformBroadcaster br;
    tf::Transform transform;
    transform.setOrigin(tf::Vector3(GPSvalues[0]-GPSvalues[2],GPSvalues[1]-GPSvalues[3],0));// 设置原点
    tf::Quaternion q(Inertialvalues[0],Inertialvalues[2],Inertialvalues[1],-Inertialvalues[3]);
    transform.setRotation(q);
    br.sendTransform(tf::StampedTransform(transform,ros::Time::now(),"odom","base_link"));
    transform.setIdentity();
    br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "base_link", "robot/Sick_LMS_291"));

}

void send_odom_data()
{
    nav_msgs::Odometry odom;
    odom.header.frame_id = "odom";
    odom.child_frame_id = "base_link";
    odom.header.stamp = ros::Time::now();
    odom.pose.pose.position.x = GPSvalues[0]-GPSvalues[2];
    odom.pose.pose.position.y = GPSvalues[1]-GPSvalues[3];
    odom.pose.pose.position.z = 0;

    odom.pose.pose.orientation.x = Inertialvalues[0];
    odom.pose.pose.orientation.y = Inertialvalues[2];
    odom.pose.pose.orientation.z = Inertialvalues[1];
    odom.pose.pose.orientation.w = -Inertialvalues[3];

    odom.twist.twist.linear.x = liner_speed;
    odom.twist.twist.angular.z = angular_speed;

    odompub.publish(odom);
}


void gpsCallback(const geometry_msgs::PointStamped::ConstPtr &value)
{
    GPSvalues[0] = value->point.x;
    GPSvalues[1] = value->point.z;
    if (gps_flag)
    {
        GPSvalues[2] = value->point.x;
        GPSvalues[3] = value->point.z;
        gps_flag=0;
    }
    broadcastTransform();  
}

void inertial_unitCallback(const sensor_msgs::Imu::ConstPtr &values)
{
    Inertialvalues[0] = values->orientation.x;
    Inertialvalues[1] = values->orientation.y;
    Inertialvalues[2] = values->orientation.z;
    Inertialvalues[3] = values->orientation.w;
    
    broadcastTransform();
}
void velCallback(const nav_msgs::Odometry::ConstPtr &value)
{
    liner_speed = value->twist.twist.linear.x;
    angular_speed = value->twist.twist.angular.z;
    send_odom_data();
}

int main(int argc, char **argv) {
   
    setlocale(LC_ALL, ""); // 用于显示中文字符
    string controllerName;
    // 在ROS网络中创建一个名为robot_init的节点
    ros::init(argc, argv, "robot_init", ros::init_options::AnonymousName);
    n = new ros::NodeHandle;
    // 截取退出信号
    signal(SIGINT, quit);

    // 订阅webots中所有可用的model_name
    ros::Subscriber nameSub = n->subscribe("model_name", 100, controllerNameCallback);
    while (controllerCount == 0 || controllerCount < nameSub.getNumPublishers()) {
        ros::spinOnce();
    }
    ros::spinOnce();
    // 服务订阅time_step和webots保持同步
    timeStepClient = n->serviceClient<webots_ros::set_int>("robot/robot/time_step");
    timeStepSrv.request.value = TIME_STEP;

    // 如果在webots中有多个控制器的话，需要让用户选择一个控制器
    if (controllerCount == 1)
        controllerName = controllerList[0];
    else {
        int wantedController = 0;
        cout << "Choose the # of the controller you want to use:\n";
        cin >> wantedController;
        if (1 <= wantedController && wantedController <= controllerCount)
        controllerName = controllerList[wantedController - 1];
        else {
        ROS_ERROR("Invalid number for controller choice.");
        return 1;
        }
    }
    ROS_INFO("Using controller: '%s'", controllerName.c_str());
    // 退出主题，因为已经不重要了
    nameSub.shutdown();

    // 使能lidar
    ros::ServiceClient lidar_Client;          
    webots_ros::set_int lidar_Srv;            
    lidar_Client = n->serviceClient<webots_ros::set_int>("/robot/Sick_LMS_291/enable");
    lidar_Srv.request.value = TIME_STEP;
    if (lidar_Client.call(lidar_Srv) && lidar_Srv.response.success) {
        ROS_INFO("gps enabled.");
    } else {
        if (!lidar_Srv.response.success)
        ROS_ERROR("Failed to enable lidar.");
        return 1;
    }

    // 订阅gps服务
    ros::ServiceClient gps_Client;          
    webots_ros::set_int gps_Srv;            
    ros::Subscriber gps_sub;
    gps_Client = n->serviceClient<webots_ros::set_int>("/robot/gps/enable");
    gps_Srv.request.value = TIME_STEP;
    if (gps_Client.call(gps_Srv) && gps_Srv.response.success) {
        ROS_INFO("gps enabled.");
        gps_sub = n->subscribe("/robot/gps/values", 1, gpsCallback);
        ROS_INFO("Topic for gps initialized.");
        while (gps_sub.getNumPublishers() == 0) {
        }
        ROS_INFO("Topic for gps connected.");
    } else {
        if (!gps_Srv.response.success)
        ROS_ERROR("Sampling period is not valid.");
        ROS_ERROR("Failed to enable gps.");
        return 1;
    }
    
    // 订阅inertial_unit服务
    ros::ServiceClient inertial_unit_Client;          
    webots_ros::set_int inertial_unit_Srv;            
    ros::Subscriber inertial_unit_sub;
    inertial_unit_Client = n->serviceClient<webots_ros::set_int>("/robot/inertial_unit/enable");
    inertial_unit_Srv.request.value = TIME_STEP;
    if (inertial_unit_Client.call(inertial_unit_Srv) && inertial_unit_Srv.response.success) {
        ROS_INFO("inertial_unit enabled.");
        inertial_unit_sub = n->subscribe("robot/inertial_unit/quaternion", 1, inertial_unitCallback);
        ROS_INFO("Topic for inertial_unit initialized.");
        while (inertial_unit_sub.getNumPublishers() == 0) {
        }
        ROS_INFO("Topic for inertial_unit connected.");
    } else {
        if (!inertial_unit_Srv.response.success)
        ROS_ERROR("Sampling period is not valid.");
        ROS_ERROR("Failed to enable inertial_unit.");
        return 1;
    }

    ros::Subscriber sub_speed;
    sub_speed = n->subscribe("/vel", 1, velCallback);
    odompub = n->advertise<nav_msgs::Odometry>("robot/odom",10);

    // main loop
    while (ros::ok()) {
        if (!timeStepClient.call(timeStepSrv) || !timeStepSrv.response.success) {
        ROS_ERROR("Failed to call service time_step for next step.");
        break;
        }
        ros::spinOnce();
    }
    
    timeStepSrv.request.value = 0;
    timeStepClient.call(timeStepSrv);
    ros::shutdown(); 
    return 0;

}

