#!/usr/bin/env python3
"""Publish synthetic ROS messages on an explicitly isolated test master only."""
import argparse
import math
import os
from urllib.parse import urlsplit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--master-uri", required=True)
    parser.add_argument("--seconds", type=float, default=12)
    args = parser.parse_args()
    master = urlsplit(args.master_uri)
    if master.hostname not in ("127.0.0.1", "localhost") or master.port in (None, 11311):
        parser.error("use an isolated localhost ROS master on a non-production port, e.g. 11321")
    os.environ["ROS_MASTER_URI"] = args.master_uri
    os.environ["ROS_IP"] = "127.0.0.1"
    import rospy
    from nav_msgs.msg import Odometry
    from sensor_msgs.msg import PointCloud2, PointField
    from sensor_msgs import point_cloud2
    from std_msgs.msg import Header
    rospy.init_node("agri_synthetic_mapping_fixture", anonymous=True, disable_signals=True)
    clouds = rospy.Publisher("/laserMapping/cloud_registered", PointCloud2, queue_size=2)
    poses = rospy.Publisher("/laserMapping/odometry", Odometry, queue_size=2)
    fields = [PointField(name, index * 4, PointField.FLOAT32, 1)
              for index, name in enumerate(("x", "y", "z", "intensity"))]
    rospy.sleep(2)
    started = rospy.get_time()
    rate = rospy.Rate(5)
    frames = 0
    while not rospy.is_shutdown() and rospy.get_time() - started < args.seconds:
        elapsed = rospy.get_time() - started
        header = Header(stamp=rospy.Time.now(), frame_id="mapping_test_world")
        points = [(x * 0.2, y * 0.2, math.sin(x * 0.1) * 0.3, float((x + y) % 255))
                  for x in range(30) for y in range(30)]
        cloud = point_cloud2.create_cloud(header, fields, points)
        pose = Odometry()
        pose.header = header
        pose.child_frame_id = "mapping_test_body"
        pose.pose.pose.position.x = math.cos(elapsed) * 2
        pose.pose.pose.position.y = math.sin(elapsed) * 2
        pose.pose.pose.position.z = 1.0
        pose.pose.pose.orientation.w = math.cos(elapsed / 2)
        pose.pose.pose.orientation.z = math.sin(elapsed / 2)
        poses.publish(pose)
        clouds.publish(cloud)
        frames += 1
        rate.sleep()
    print("SYNTHETIC_FIXTURE_FRAMES={}".format(frames), flush=True)


if __name__ == "__main__":
    main()
