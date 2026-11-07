import importlib.util
import math
import socket
import subprocess
import sys
import time
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

    def test_real_listeners_keep_pose_separate_from_cloud(self):
        # 独立 demo 进程，只使用本地临时端口，不连接 ROS 或飞控。
        for _ in range(40):
            probe = socket.socket(); probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
            other = socket.socket()
            try:
                if port > 65533: continue
                other.bind(('127.0.0.1', port+2)); break
            except OSError: pass
            finally: probe.close(); other.close()
        else: self.fail('no adjacent local test ports')
        path = str(Path(__file__).resolve().parents[1] / 'deploy/pointcloud_bridge.py')
        child = subprocess.Popen([sys.executable, path, '--demo', '--port', str(port)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        connections = []
        try:
            deadline = time.monotonic() + 8
            for target in (port, port+2):
                while True:
                    try:
                        peer = socket.create_connection(('127.0.0.1', target), timeout=.5)
                        peer.settimeout(2); connections.append(peer); break
                    except OSError:
                        if time.monotonic() > deadline: raise
                        time.sleep(.05)
            def read_until(peer, expected):
                decoder = bridge.FrameDecoder(); frames = []
                while time.monotonic() < deadline and not any(h.get('type') == expected for h, _ in frames):
                    chunk = peer.recv(262144)
                    if not chunk: break
                    frames.extend(decoder.feed(chunk))
                return frames
            cloud = read_until(connections[0], 'cloud')
            pose = read_until(connections[1], 'pose')
            self.assertTrue(any(h['type'] == 'cloud' for h, _ in cloud))
            self.assertTrue(any(h['type'] == 'pose' for h, _ in pose))
            self.assertTrue(all(h['type'] in ('hello','pose','reset') and not payload for h,payload in pose))
            self.assertEqual(cloud[0][0]['session'], pose[0][0]['session'])
        finally:
            for peer in connections: peer.close()
            child.terminate(); child.wait(timeout=5)

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

    def test_pose_only_peer_shares_session_but_never_receives_cloud(self):
        core = bridge.MapBridge()
        core.publish_cloud([(0, 0, 0, 10)], 1)
        core.publish_pose((1, 2, 3), (0, 0, 0, 1), 1)
        cloud, pose = CapturePeer(), CapturePeer()
        self.assertTrue(core.add_peer(cloud))
        self.assertTrue(core.add_pose_peer(pose))
        core.publish_cloud([(1, 0, 0, 20)], 2)
        core.publish_pose((2, 2, 3), (0, 0, 0, 1), 2)
        frames = pose.frames()
        self.assertEqual([header['type'] for header, _ in frames], ['hello', 'pose', 'pose'])
        self.assertTrue(all(not payload for _, payload in frames))
        self.assertTrue(all(header['session'] == core.session for header, _ in frames))
        self.assertTrue(all(header['frame'] == 'world' for header, _ in frames))
        old_session = core.session
        core.reset_source('new map')
        core.publish_pose((3, 2, 3), (0, 0, 0, 1), 3)
        frames = pose.frames()
        self.assertNotEqual(old_session, core.session)
        self.assertEqual([header['type'] for header, _ in frames[-2:]], ['reset', 'pose'])
        self.assertTrue(all(header['session'] == core.session for header, _ in frames[-2:]))
        self.assertIn('cloud', [header['type'] for header, _ in cloud.frames()])

    def test_pose_only_peer_does_not_wait_for_snapshot(self):
        core = bridge.MapBridge()
        self.assertTrue(core.publish_cloud([(0, 0, 0, 1)], 1))
        entered, release, added = threading.Event(), threading.Event(), threading.Event()
        original = core._payload
        def slow_snapshot(points):
            entered.set()
            if not release.wait(3): raise RuntimeError('test timeout')
            return original(points)
        core._payload = slow_snapshot
        cloud = CapturePeer()
        pose = CapturePeer()
        connector = threading.Thread(target=lambda: core.add_peer(cloud))
        pose_connector = threading.Thread(target=lambda: (core.add_pose_peer(pose), added.set()))
        connector.start()
        try:
            self.assertTrue(entered.wait(2))
            pose_connector.start()
            self.assertTrue(added.wait(1))
        finally:
            release.set()
            connector.join(3)
            if pose_connector.ident is not None: pose_connector.join(3)
        self.assertEqual([h['type'] for h, _ in pose.frames()], ['hello'])

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

    def test_concurrent_cloud_and_pose_preserve_sequence_order(self):
        core = bridge.MapBridge()
        peer = CapturePeer()
        self.assertTrue(core.add_peer(peer))
        entered = threading.Event()
        release = threading.Event()
        pose_started = threading.Event()
        pose_done = threading.Event()
        original_packet = core._packet

        def delay_cloud(kind, *args, **kwargs):
            packet = original_packet(kind, *args, **kwargs)
            if kind == 'cloud':
                entered.set()
                if not release.wait(3):
                    raise RuntimeError('test timed out')
            return packet

        core._packet = delay_cloud
        cloud = threading.Thread(target=lambda: core.publish_cloud([(0, 0, 0, 1)], 1))
        pose = threading.Thread(target=lambda: (pose_started.set(), core.publish_pose((0, 0, 0), (0, 0, 0, 1), 1), pose_done.set()))
        cloud.start()
        try:
            self.assertTrue(entered.wait(2))
            pose.start()
            self.assertTrue(pose_started.wait(2))
            pose_done.wait(0.2)
        finally:
            release.set()
        cloud.join(3)
        if pose.ident is not None:
            pose.join(3)
        self.assertFalse(cloud.is_alive())
        self.assertFalse(pose.is_alive())
        frames = peer.frames()
        sequences = [header['seq'] for header, _ in frames]
        self.assertEqual(sequences, sorted(set(sequences)))
        self.assertEqual([header['type'] for header, _ in frames[-2:]], ['cloud', 'pose'])

    def test_pose_can_publish_while_snapshot_payload_is_built(self):
        core = bridge.MapBridge()
        existing, newcomer = CapturePeer(), CapturePeer()
        self.assertTrue(core.add_peer(existing))
        self.assertTrue(core.publish_cloud([(0, 0, 0, 1)], 1))
        entered = threading.Event()
        release = threading.Event()
        pose_done = threading.Event()
        original_payload = core._payload

        def delay_snapshot(points):
            entered.set()
            if not release.wait(3):
                raise RuntimeError('test timed out')
            return original_payload(points)

        core._payload = delay_snapshot
        connector = threading.Thread(target=lambda: core.add_peer(newcomer))
        producer = threading.Thread(target=lambda: (core.publish_pose((1, 0, 0), (0, 0, 0, 1), 1), pose_done.set()))
        connector.start()
        try:
            self.assertTrue(entered.wait(2))
            producer.start()
            pose_remained_responsive = pose_done.wait(1)
        finally:
            release.set()
        connector.join(3)
        if producer.ident is not None:
            producer.join(3)
        self.assertTrue(pose_remained_responsive)
        self.assertFalse(connector.is_alive())
        self.assertFalse(producer.is_alive())
        frames = newcomer.frames()
        self.assertEqual([header['type'] for header, _ in frames], ['hello', 'cloud', 'pose', 'status'])
        self.assertTrue(frames[1][0]['snapshot'])
        self.assertEqual([header['seq'] for header, _ in frames], sorted({header['seq'] for header, _ in frames}))

    def test_reset_and_concurrent_pose_preserve_new_session_order(self):
        core = bridge.MapBridge()
        peer = CapturePeer()
        self.assertTrue(core.add_peer(peer))
        entered = threading.Event()
        release = threading.Event()
        pose_started = threading.Event()
        pose_done = threading.Event()
        original_packet = core._packet

        def delay_reset(kind, *args, **kwargs):
            packet = original_packet(kind, *args, **kwargs)
            if kind == 'reset':
                entered.set()
                if not release.wait(3):
                    raise RuntimeError('test timed out')
            return packet

        core._packet = delay_reset
        reset = threading.Thread(target=lambda: core.reset_source('test reset'))
        pose = threading.Thread(target=lambda: (pose_started.set(), core.publish_pose((0, 0, 0), (0, 0, 0, 1), 1), pose_done.set()))
        reset.start()
        try:
            self.assertTrue(entered.wait(2))
            pose.start()
            self.assertTrue(pose_started.wait(2))
            pose_done.wait(0.2)
        finally:
            release.set()
        reset.join(3)
        if pose.ident is not None:
            pose.join(3)
        self.assertFalse(reset.is_alive())
        self.assertFalse(pose.is_alive())
        headers = [header for header, _ in peer.frames()]
        self.assertEqual([h['type'] for h in headers[-2:]], ['reset', 'pose'])
        self.assertEqual([h['seq'] for h in headers[-2:]], [0, 1])
        self.assertEqual(headers[-2]['session'], headers[-1]['session'])

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


class DenseAlignmentTests(unittest.TestCase):
    @staticmethod
    def message(stamp=1.0, frame='body'):
        msg = CloudSamplingTests.message(1, 1, struct.pack('<ffff', 1, 0, 0, 80))
        msg.header = SimpleNamespace(frame_id=frame, stamp=SimpleNamespace(to_sec=lambda: stamp))
        return msg

    def test_rotation_translation_and_exact_time_pose(self):
        aligner = bridge.BodyCloudAligner()
        # A later pose must not replace the matching pose for an earlier scan.
        aligner.add_pose(1, 'world', 'body', (10, 20, 30), (0, 0, math.sqrt(.5), math.sqrt(.5)))
        aligner.add_pose(1.1, 'world', 'body', (99, 99, 99), (0, 0, 0, 1))
        points, frame = aligner.transform(self.message())
        self.assertEqual(frame, 'world')
        point = next(points)
        for actual, expected in zip(point, (10, 21, 30, 80)):
            self.assertAlmostEqual(actual, expected)

    def test_requires_matching_body_frame_and_recent_pose(self):
        aligner = bridge.BodyCloudAligner()
        self.assertIsNone(aligner.transform(self.message()))
        aligner.add_pose(1, 'world', 'body', (0, 0, 0), (0, 0, 0, 1))
        self.assertIsNone(aligner.transform(self.message(1.1)))
        self.assertIsNone(aligner.transform(self.message(frame='lidar')))
        aligner.clear()
        self.assertIsNone(aligner.transform(self.message()))

    def test_fine_map_preserves_points_separated_by_five_centimeters(self):
        core = bridge.MapBridge()
        core.publish_cloud([(0.01, 0, 0, 1), (0.06, 0, 0, 2)], 1)
        self.assertEqual(len(core.points), 2)


if __name__ == "__main__":
    unittest.main()
