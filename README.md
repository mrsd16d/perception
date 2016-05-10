# PERCH: Perception via Search for Multi-Object Recognition and Localization

Overview
--------
This repository provides the implementation for <a href="http://arxiv.org/abs/1510.05613">PERCH</a>.

The package 'kinect_sim' is based on PCL's simulation API and serves as the depth-image renderer/simulator for PERCH. This provides the functionality to generate the depth-image for a scene, given a camera pose, set of 3D models/meshes and their poses.

The 'sbpl_perception' package implements the PERCH algorithm. This works in conjunction with the SBPL planning library (https://github.com/sbpl/sbpl) and provides the 'search environment'. The documentation in the SBPL github page provides examples of the planner-environment usage.  

Dependencies
------------

- ROS Hydro+
- C++11
- OpenCV 2.x
- PCL 1.7+

Setup
-----

1. Get ROS Hydro (or Indigo) from http://wiki.ros.org/hydro/Installation/Ubuntu
2. Set up a catkin workspace ~/my_workspace (http://wiki.ros.org/catkin/Tutorials/create_a_workspace).
3. Download the <a href="https://raw.githubusercontent.com/venkatrn/perception/master/perch.rosinstall" download="perch.rosinstall">rosinstall file</a> to your workspace.

```bash
cd ~/my_workspace
wstool init src
wstool merge -t src perch.rosinstall
wstool update -t src
catkin_make
```

Demo
----

An example scene is provided under sbpl_perception/demo. To run PERCH, 

Example usage:
```bash
roscd sbpl_perception && mkdir visualization
roslaunch sbpl_perception demo.launch 
```

The states 'expanded' as well as the goal state will be saved under sbpl_perception/visualization. The expanded states will also show up on an OpenCV window named "Expansions". To also save all the 'generated' (rendered) states to sbpl_perception/visualization, use

```bash
roslaunch sbpl_perception demo.launch image_debug:=true
```
(TODO: more details)

Run ```wstool update -t src``` to pull changes from all related repositories.
