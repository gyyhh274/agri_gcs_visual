import importlib.util
import math
from pathlib import Path
import struct
import threading
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location("pointcloud_bridge", Path(__file__).resolve().parents[1] / "deploy/pointcloud_bridge.py")
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class CapturePeer:
    def __init__(self):
        self.packets = []
        self.closed = False

    def offer(self, packet):
        if self.closed:
            return False
        self.packets.append(packet)
        return True

    def close(self):
        self.closed = True

    def frames(self):
        decoder = bridge.FrameDecoder()
        return decoder.feed(b"".join(self.packets))


class DummySocket:
    def __init__(self):
        self.closed = False

    def shutdown(self, unused):
        pass

    def close(self):
        self.closed = True


class ProtocolTests(unittest.TestCase):
    def test_fragmented_binary_and_coalesced_frames(self):
        payload = bridge.POINT.pack(1.25, -2, 3, 255)
        packet = bridge.encode_frame({"protocol": 1, "type": "cloud", "point_count": 1}, payload)
        decoder = bridge.FrameDecoder()
        result = []
        for byte in packet:
            result.extend(decoder.feed(bytes([byte])))
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0][1], payload)
        self.assertEqual(struct.unpack("<ffff", result[0][1]), (1.25, -2, 3, 255))
        self.assertEqual(len(decoder.feed(packet + packet)), 2)

    def test_invalid_prefix_and_oversize_rejected(self):
        for prefix in (bridge.PREFIX.pack(b"FAIL", 10, 0),
                       bridge.PREFIX.pack(b"AGM1", 0, 0),
                       bridge.PREFIX.pack(b"AGM1", 10, bridge.MAX_PAYLOAD + 1)):
            with self.subTest(prefix=prefix), self.assertRaises(ValueError):
                bridge.FrameDecoder().feed(prefix)


class MapTests(unittest.TestCase):
    def test_voxel_replacement_floor_and_fifo_limit(self):
        core = bridge.MapBridge(voxel_size=1, maximum_points=3)
        self.assertTrue(core.publish_cloud([(0.1, 0.1, 0.1, 1), (0.9, 0.1, 0.1, 2),
                                           (-0.1, 0, 0, 3), (1.1, 0, 0, 4)], 1))
        self.assertEqual(len(core.points), 3)
        self.assertEqual(core.points[(0, 0, 0)][3], 2)
        self.assertIn((-1, 0, 0), core.points)
        core.publish_cloud([(2.1, 0, 0, 5)], 2)
        self.assertEqual(len(core.points), 3)
        self.assertNotIn((0, 0, 0), core.points)

    def test_invalid_points_do_not_mark_source_live(self):
        core = bridge.MapBridge()
        self.assertFalse(core.publish_cloud([(math.nan, 0, 0, 0), (0, math.inf, 0, 0),
                                            (0, 0, 0, 1e200)], 1))
        self.assertEqual(core.status()["state"], "waiting_ros")
        self.assertEqual(len(core.points), 0)

    def test_connect_snapshot_then_delta_and_common_schema(self):
        core = bridge.MapBridge()
        core.publish_cloud([(0, 0, 0, 10)], 1)
        peer = CapturePeer()
        self.assertTrue(core.add_peer(peer))
        core.publish_cloud([(1, 0, 0, 20)], 2)
        frames = peer.frames()
        self.assertEqual([header["type"] for header, _ in frames], ["hello", "cloud", "status", "cloud"])
        self.assertTrue(frames[1][0]["snapshot"])
        self.assertFalse(frames[-1][0]["snapshot"])
        self.assertEqual(frames[1][0]["point_count"], 1)
        self.assertEqual(len(frames[1][1]), 16)
        for header, _ in frames:
            self.assertEqual(header["protocol"], 1)
            self.assertEqual(header["frame"], "world")
            self.assertFalse(header["demo"])
            self.assertEqual(header["session"], core.session)
            self.assertTrue(math.isfinite(header["stamp"]))
        self.assertEqual([header["seq"] for header, _ in frames], sorted({header["seq"] for header, _ in frames}))

    def test_snapshot_atomic_order_with_concurrent_publication(self):
        core = bridge.MapBridge()
        entered = threading.Event()
        release = threading.Event()

        class BlockingPeer(CapturePeer):
            def offer(self, packet):
                if not self.packets:
                    entered.set()
                    if not release.wait(2):
                        raise RuntimeError("test timed out")
                return super().offer(packet)

        peer = BlockingPeer()
        connector = threading.Thread(target=lambda: core.add_peer(peer))
        connector.start()
        self.assertTrue(entered.wait(2))
        producer = threading.Thread(target=lambda: core.publish_cloud([(1, 0, 0, 1)], 1))
        producer.start()
        release.set()
        connector.join(2)
        producer.join(2)
        self.assertFalse(connector.is_alive())
        self.assertFalse(producer.is_alive())
        clouds = [header for header, _ in peer.frames() if header["type"] == "cloud"]
        self.assertEqual([item["snapshot"] for item in clouds], [True, False])
        self.assertEqual([item["point_count"] for item in clouds], [0, 1])

    def test_source_restart_and_frame_change_create_new_sessions(self):
        core = bridge.MapBridge()
        peer = CapturePeer()
        core.add_peer(peer)
        core.publish_cloud([(1, 0, 0, 1)], 10)
        first_session = core.session
        core.publish_cloud([(2, 0, 0, 1)], 9.9)
        self.assertNotEqual(core.session, first_session)
        self.assertEqual(len(core.points), 1)
        second_session = core.session
        core.publish_cloud([(3, 0, 0, 1)], 11, "map")
        self.assertNotEqual(core.session, second_session)
        self.assertEqual(core.frame, "map")
        self.assertEqual(len(core.points), 1)
        resets = [header for header, _ in peer.frames() if header["type"] == "reset"]
        self.assertEqual(len(resets), 2)

    def test_publisher_sequence_restart_with_increasing_timestamp(self):
        core = bridge.MapBridge()
        core.observe_source(100, "/laserMapping")
        core.publish_cloud([(1, 0, 0, 1)], 100)
        previous = core.session
        core.observe_source(0, "/laserMapping")
        core.publish_cloud([(50, 0, 0, 1)], 101)
        self.assertNotEqual(core.session, previous)
        self.assertEqual(len(core.points), 1)
        self.assertEqual(next(iter(core.points.values()))[0], 50)
        previous = core.session
        core.observe_source(1, "/laserMapping")
        self.assertEqual(core.session, previous)
        core.observe_source(2, "/another_mapper")
        self.assertNotEqual(core.session, previous)
        self.assertEqual(len(core.points), 0)

    def test_heartbeats_and_pose_do_not_refresh_cloud_freshness(self):
        now = [100.0]
        core = bridge.MapBridge(clock=lambda: now[0])
        self.assertEqual(core.status()["state"], "waiting_ros")
        core.set_ros(True, "ready")
        self.assertEqual(core.status()["state"], "waiting_cloud")
        core.publish_cloud([(0, 0, 0, 1)], 1)
        self.assertEqual(core.status()["state"], "live")
        now[0] += 4
        core.heartbeat()
        core.publish_pose((1, 2, 3), (0, 0, 0, 1), 2)
        self.assertEqual(core.status()["state"], "stale")
        core.set_ros(False, "master down")
        self.assertEqual(core.status()["state"], "waiting_ros")
        peer = CapturePeer()
        core.add_peer(peer)
        snapshot = next(header for header, _ in peer.frames() if header["type"] == "cloud")
        self.assertTrue(snapshot["stale"])

    def test_pose_frame_and_quaternion_validation_and_cache_age(self):
        now = [10.0]
        core = bridge.MapBridge(clock=lambda: now[0])
        self.assertFalse(core.publish_pose((1, 2, 3), (0, 0, 0, 1), 1, "other"))
        self.assertFalse(core.publish_pose((1, 2, 3), (0, 0, 0, 0), 1))
        self.assertTrue(core.publish_pose((1, 2, 3), (0, 0, 0, 2), 1))
        self.assertEqual(core.pose["orientation"], [0, 0, 0, 1])
        now[0] += 4
        peer = CapturePeer()
        core.add_peer(peer)
        self.assertNotIn("pose", [header["type"] for header, _ in peer.frames()])

    def test_client_limit_and_explicit_demo_label(self):
        core = bridge.MapBridge(maximum_clients=1, demo=True)
        first, second = CapturePeer(), CapturePeer()
        self.assertTrue(core.add_peer(first))
        self.assertFalse(core.add_peer(second))
        self.assertTrue(second.closed)
        self.assertTrue(all(header["demo"] for header, _ in first.frames()))


class BackpressureTests(unittest.TestCase):
    def test_slow_client_queue_is_bounded_and_socket_closed(self):
        connection = DummySocket()
        peer = bridge.Peer(connection, maximum_bytes=10, maximum_frames=3)
        self.assertTrue(peer.offer(b"123456"))
        self.assertFalse(peer.offer(b"123456"))
        self.assertTrue(peer.closed)
        self.assertTrue(connection.closed)
        self.assertEqual(peer.pending_bytes, 0)
        self.assertFalse(peer.offer(b"x"))

    def test_frame_count_limit_bounds_tiny_frames(self):
        peer = bridge.Peer(DummySocket(), maximum_bytes=1000, maximum_frames=2)
        self.assertTrue(peer.offer(b"a"))
        self.assertTrue(peer.offer(b"b"))
        self.assertFalse(peer.offer(b"c"))


class CloudSamplingTests(unittest.TestCase):
    @staticmethod
    def message(width, height, data, endian=False, row_padding=0):
        fields = [SimpleNamespace(name=name, datatype=7, count=1, offset=index * 4)
                  for index, name in enumerate(("x", "y", "z", "intensity"))]
        return SimpleNamespace(fields=fields, width=width, height=height, data=data,
                               point_step=16, row_step=width * 16 + row_padding, is_bigendian=endian)

    def test_organized_big_endian_rows_and_sampling_bound(self):
        row1 = struct.pack(">ffffffff", 1, 2, 3, 4, 5, 6, 7, 8) + b"PADD"
        row2 = struct.pack(">ffffffff", 9, 10, 11, 12, 13, 14, 15, 16) + b"PADD"
        msg = self.message(2, 2, row1 + row2, endian=True, row_padding=4)
        self.assertEqual(list(bridge.sampled_cloud(msg)), [(1, 2, 3, 4), (5, 6, 7, 8),
                                                         (9, 10, 11, 12), (13, 14, 15, 16)])
        self.assertEqual(len(list(bridge.sampled_cloud(msg, maximum=2))), 2)

    def test_missing_intensity_and_truncated_payload(self):
        msg = self.message(1, 1, struct.pack("<ffff", 1, 2, 3, 4))
        msg.fields = msg.fields[:3]
        self.assertEqual(list(bridge.sampled_cloud(msg)), [(1, 2, 3, 0)])
        msg.data = msg.data[:5]
        self.assertEqual(list(bridge.sampled_cloud(msg)), [])


if __name__ == "__main__":
    unittest.main()
