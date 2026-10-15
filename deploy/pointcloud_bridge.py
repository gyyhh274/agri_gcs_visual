#!/usr/bin/env python3
"""Read-only ROS point cloud bridge. ROS is optional; demo is always explicit."""
import argparse
from collections import OrderedDict, deque
import json
import math
import os
import signal
import socket
import struct
import threading
import time
import uuid
import xmlrpc.client

PREFIX = struct.Struct("<4sII")
POINT = struct.Struct("<ffff")
MAX_HEADER = 16384
MAX_PAYLOAD = 8 * 1024 * 1024


def encode_frame(header, payload=b""):
    raw = json.dumps(header, separators=(",", ":"), ensure_ascii=False,
                     allow_nan=False).encode("utf-8")
    if len(raw) > MAX_HEADER or len(payload) > MAX_PAYLOAD:
        raise ValueError("mapping frame exceeds protocol limits")
    return PREFIX.pack(b"AGM1", len(raw), len(payload)) + raw + payload


class FrameDecoder:
    """Incremental decoder used by diagnostics and tests; never executes input."""
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= PREFIX.size:
            magic, header_size, payload_size = PREFIX.unpack_from(self.buffer)
            if magic != b"AGM1" or not 0 < header_size <= MAX_HEADER or payload_size > MAX_PAYLOAD:
                raise ValueError("invalid mapping frame prefix")
            size = PREFIX.size + header_size + payload_size
            if len(self.buffer) < size:
                break
            header = json.loads(bytes(self.buffer[PREFIX.size:PREFIX.size + header_size]))
            frames.append((header, bytes(self.buffer[PREFIX.size + header_size:size])))
            del self.buffer[:size]
        return frames


class Peer:
    """One bounded outbound queue; a slow receiver cannot block ROS callbacks."""
    def __init__(self, connection, maximum_bytes=8 * 1024 * 1024, maximum_frames=32):
        self.connection = connection
        self.maximum_bytes = maximum_bytes
        self.maximum_frames = maximum_frames
        self.pending = deque()
        self.pending_bytes = 0
        self.closed = False
        self.condition = threading.Condition()

    def offer(self, packet):
        with self.condition:
            if self.closed:
                return False
            if self.pending_bytes + len(packet) > self.maximum_bytes or len(self.pending) >= self.maximum_frames:
                self._close_locked()
                return False
            self.pending.append(packet)
            self.pending_bytes += len(packet)
            self.condition.notify()
            return True

    def _close_locked(self):
        self.closed = True
        self.pending.clear()
        self.pending_bytes = 0
        try:
            self.connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.connection.close()
        self.condition.notify_all()

    def close(self):
        with self.condition:
            if not self.closed:
                self._close_locked()

    def run(self):
        try:
            while True:
                with self.condition:
                    self.condition.wait_for(lambda: self.closed or self.pending)
                    if self.closed:
                        return
                    packet = self.pending.popleft()
                    self.pending_bytes -= len(packet)
                self.connection.sendall(packet)
        except OSError:
            pass
        finally:
            self.close()


class MapBridge:
    def __init__(self, voxel_size=0.15, maximum_points=250000, maximum_clients=4,
                 demo=False, clock=time.monotonic):
        self.lock = threading.RLock()
        self.clock = clock
        self.voxel_size = voxel_size
        self.maximum_points = maximum_points
        self.maximum_clients = maximum_clients
        self.demo = demo
        self.points = OrderedDict()
        self.clients = []
        self.session = str(uuid.uuid4())
        self.sequence = 0
        self.frame = "world"
        self.last_stamp = None
        self.last_cloud_time = None
        self.pose = None
        self.last_pose_time = None
        self.last_source_sequence = None
        self.source_caller = ""
        self.ros_ready = False
        self.ros_message = "等待 ROS master 与建图节点"

    def _packet(self, kind, payload=b"", **fields):
        header = dict(protocol=1, type=kind, session=self.session, frame=self.frame,
                      stamp=time.time(), seq=self.sequence, demo=self.demo)
        self.sequence += 1
        header.update(fields)
        return encode_frame(header, payload)

    def _broadcast(self, packet):
        self.clients = [peer for peer in self.clients if peer.offer(packet)]

    def _reset(self, frame, reason):
        self.session = str(uuid.uuid4())
        self.sequence = 0
        self.frame = frame
        self.points.clear()
        self.pose = None
        self.last_pose_time = None
        self.last_stamp = None
        self.last_cloud_time = None
        self._broadcast(self._packet("reset", reason=reason))

    def set_ros(self, ready, message):
        with self.lock:
            self.ros_ready = ready
            self.ros_message = message

    def reset_source(self, reason):
        with self.lock:
            self._reset(self.frame, reason)
            self.last_source_sequence = None
            self.source_caller = ""

    def observe_source(self, sequence, caller):
        """Detect a publisher restart even when its frame and wall clock continue."""
        if not isinstance(sequence, int) or sequence < 0:
            return
        with self.lock:
            changed = self.source_caller and caller and caller != self.source_caller
            rolled_back = self.last_source_sequence is not None and sequence < self.last_source_sequence
            if changed or rolled_back:
                self._reset(self.frame, "ROS point cloud publisher changed or restarted")
            self.last_source_sequence = sequence
            self.source_caller = caller

    def status(self):
        if self.demo:
            state = "live" if self.last_cloud_time is not None else "waiting_cloud"
            message = "合成演示数据 · 非机载点云"
        elif self.last_cloud_time is not None and self.clock() - self.last_cloud_time <= 3:
            state, message = "live", "正在接收机载点云"
        elif not self.ros_ready:
            state, message = "waiting_ros", self.ros_message
        elif self.last_cloud_time is None:
            state, message = "waiting_cloud", "ROS 已连接，等待 /laserMapping/cloud_registered"
        else:
            state, message = "stale", "机载点云已超过 3 秒未更新"
        return dict(state=state, message=message, point_count=len(self.points))

    def heartbeat(self):
        with self.lock:
            self._broadcast(self._packet("status", **self.status()))

    def add_peer(self, peer):
        # Hold the same lock as publish_cloud: snapshot always precedes all newer deltas.
        with self.lock:
            self.clients = [item for item in self.clients if not item.closed]
            if len(self.clients) >= self.maximum_clients:
                peer.close()
                return False
            packets = [self._packet("hello", voxel_size=self.voxel_size,
                                    max_points=self.maximum_points),
                       self._packet("cloud", self._payload(self.points.values()),
                                    point_count=len(self.points), snapshot=True,
                                    stale=self.last_cloud_time is None or self.clock() - self.last_cloud_time > 3)]
            if self.pose and self.last_pose_time is not None and self.clock() - self.last_pose_time <= 3:
                packets.append(self._packet("pose", **self.pose))
            packets.append(self._packet("status", **self.status()))
            for packet in packets:
                if not peer.offer(packet):
                    peer.close()
                    return False
            self.clients.append(peer)
            return True

    @staticmethod
    def _payload(points):
        return b"".join(POINT.pack(*point) for point in points)

    def publish_cloud(self, points, stamp, frame="world"):
        if not math.isfinite(stamp) or not frame or len(frame) > 128:
            return False
        with self.lock:
            if frame != self.frame:
                self._reset(frame, "source coordinate frame changed")
            elif self.last_stamp is not None and stamp < self.last_stamp - 0.000001:
                self._reset(frame, "source timestamp moved backwards")
            changes = OrderedDict()
            for index, point in enumerate(points):
                if index >= 8000:
                    break
                if len(point) != 4 or not all(math.isfinite(value) for value in point):
                    continue
                if any(abs(value) > 1000000 for value in point):
                    continue
                values = tuple(float(value) for value in point)
                key = tuple(math.floor(value / self.voxel_size) for value in values[:3])
                self.points[key] = values
                changes[key] = values
                while len(self.points) > self.maximum_points:
                    self.points.popitem(last=False)
            if not changes:
                return False
            self.last_stamp = stamp
            self.last_cloud_time = self.clock()
            self._broadcast(self._packet("cloud", self._payload(changes.values()), stamp=stamp,
                                         point_count=len(changes), snapshot=False))
            return True

    def publish_pose(self, position, orientation, stamp, frame="world"):
        if len(position) != 3 or len(orientation) != 4 or not math.isfinite(stamp):
            return False
        if not all(math.isfinite(value) for value in list(position) + list(orientation)):
            return False
        norm = math.sqrt(sum(value * value for value in orientation))
        if not math.isfinite(norm) or norm < 1e-9 or any(abs(value) > 1000000 for value in position):
            return False
        with self.lock:
            if frame != self.frame:
                return False
            if self.last_stamp is not None and stamp < self.last_stamp - 3:
                return False
            self.pose = dict(position=list(position), orientation=[value / norm for value in orientation], stamp=stamp)
            self.last_pose_time = self.clock()
            self._broadcast(self._packet("pose", **self.pose))
            return True

    def close(self):
        with self.lock:
            for peer in self.clients:
                peer.close()
            self.clients.clear()


def serve(bridge, listener, stop):
    listener.settimeout(0.5)
    while not stop.is_set():
        try:
            connection, _ = listener.accept()
        except socket.timeout:
            continue
        except OSError:
            return
        connection.settimeout(3)
        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        peer = Peer(connection)
        if bridge.add_peer(peer):
            threading.Thread(target=peer.run, daemon=True, name="mapping-client").start()


def sampled_cloud(message, maximum=8000):
    """Decode only sampled points, including organized clouds and endian variants."""
    fields = {field.name: field for field in message.fields}
    types = {1: "b", 2: "B", 3: "h", 4: "H", 5: "i", 6: "I", 7: "f", 8: "d"}
    readers = []
    for name in ("x", "y", "z", "intensity"):
        field = fields.get(name)
        if field is None:
            if name == "intensity":
                readers.append(None)
                continue
            return
        code = types.get(field.datatype)
        if code is None or field.count < 1:
            return
        decoder = struct.Struct((">" if message.is_bigendian else "<") + code)
        if field.offset < 0 or field.offset + decoder.size > message.point_step:
            return
        readers.append((decoder, field.offset))
    total = message.width * message.height
    if total <= 0 or message.point_step <= 0 or message.row_step < message.width * message.point_step:
        return
    stride = max(1, math.ceil(total / maximum))
    data = memoryview(message.data)
    for index in range(0, total, stride):
        row, column = divmod(index, message.width)
        offset = row * message.row_step + column * message.point_step
        if offset + message.point_step > len(data):
            return
        yield tuple(float(reader[0].unpack_from(data, offset + reader[1])[0]) if reader else 0.0
                    for reader in readers)


class TimeoutTransport(xmlrpc.client.Transport):
    def make_connection(self, host):
        connection = super().make_connection(host)
        connection.timeout = 2
        return connection


def master_available(uri):
    try:
        with xmlrpc.client.ServerProxy(uri, transport=TimeoutTransport()) as master:
            return master.getPid("/agri_pointcloud_bridge")[0] == 1
    except (OSError, ValueError, xmlrpc.client.Error):
        return False


def ros_worker(bridge, stop, max_hz):
    # Import ROS only here: TCP status works even without ROS packages or a master.
    try:
        import rospy
        from sensor_msgs.msg import PointCloud2
        from nav_msgs.msg import Odometry
    except ImportError as exc:
        bridge.set_ros(False, "ROS 依赖不可用：" + str(exc))
        return

    last_cloud = [0.0]
    last_pose = [0.0]
    callback_lock = threading.Lock()

    def cloud_callback(message):
        now = time.monotonic()
        with callback_lock:
            connection_header = getattr(message, "_connection_header", {}) or {}
            bridge.observe_source(message.header.seq, connection_header.get("callerid", ""))
            if now - last_cloud[0] < 1.0 / max_hz:
                return
            last_cloud[0] = now
        try:
            bridge.publish_cloud(sampled_cloud(message), message.header.stamp.to_sec(),
                                 message.header.frame_id.lstrip("/"))
        except (ValueError, TypeError, struct.error, OverflowError) as exc:
            bridge.set_ros(True, "忽略无效点云：" + str(exc))

    def pose_callback(message):
        now = time.monotonic()
        with callback_lock:
            if now - last_pose[0] < 0.05:
                return
            last_pose[0] = now
        p, q = message.pose.pose.position, message.pose.pose.orientation
        bridge.publish_pose((p.x, p.y, p.z), (q.x, q.y, q.z, q.w),
                            message.header.stamp.to_sec(), message.header.frame_id.lstrip("/"))

    initialized = False
    reconnect_reset_needed = False
    subscribers = []
    master_uri = os.environ.get("ROS_MASTER_URI", "http://127.0.0.1:11311")
    while not stop.is_set():
        available = master_available(master_uri)
        if not available:
            bridge.set_ros(False, "等待 ROS master；不会启动飞行或建图程序")
            if subscribers:
                reconnect_reset_needed = True
                for subscriber in subscribers:
                    subscriber.unregister()
                subscribers = []
        elif not subscribers:
            try:
                if reconnect_reset_needed:
                    bridge.reset_source("ROS master reconnected")
                    reconnect_reset_needed = False
                if not initialized:
                    rospy.init_node("agri_pointcloud_bridge", anonymous=True,
                                    disable_signals=True, log_level=rospy.WARN)
                    initialized = True
                subscribers = [
                    rospy.Subscriber("/laserMapping/cloud_registered", PointCloud2,
                                     cloud_callback, queue_size=1, buff_size=8 * 1024 * 1024),
                    rospy.Subscriber("/laserMapping/odometry", Odometry,
                                     pose_callback, queue_size=1)]
                bridge.set_ros(True, "ROS 已连接，等待点云与定位")
            except Exception as exc:
                bridge.set_ros(False, "ROS 接入暂不可用：" + str(exc))
        stop.wait(1)
    for subscriber in subscribers:
        subscriber.unregister()
    if initialized:
        rospy.signal_shutdown("mapping bridge stopped")


def demo_worker(bridge, stop):
    """Explicit synthetic grove used only with --demo, never a fallback."""
    scene = []
    for x in range(-100, 101):
        for y in range(-80, 81):
            px, py = x * 0.2, y * 0.2
            scene.append((px, py, -0.3 + 0.08 * math.sin(px / 4) * math.cos(py / 4), 20.0))
    for tx, ty in ((-8, -6), (-2, -8), (6, -5), (10, 3), (1, 5), (-7, 6)):
        for z in range(50):
            for angle in range(20):
                theta = angle * math.tau / 20
                radius = 0.28 + 0.06 * math.sin(z * 0.4)
                scene.append((tx + radius * math.cos(theta), ty + radius * math.sin(theta), z * 0.13, 110.0))
        for layer in range(15):
            for angle in range(80):
                theta = angle * math.tau / 80
                radius = 0.8 + 1.8 * math.sin(layer / 14 * math.pi)
                scene.append((tx + radius * math.cos(theta), ty + radius * math.sin(theta), 4.7 + layer * 0.2, 65.0))
    cursor = tick = 0
    while not stop.is_set():
        now = time.time()
        points = [scene[(cursor + i) % len(scene)] for i in range(5000)]
        cursor = (cursor + 5000) % len(scene)
        bridge.publish_cloud(points, now)
        angle = tick * 0.025
        bridge.publish_pose((7 * math.cos(angle), 7 * math.sin(angle), 2.0),
                            (0, 0, math.sin((angle + math.pi / 2) / 2), math.cos((angle + math.pi / 2) / 2)), now)
        tick += 1
        stop.wait(0.2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=37262)
    parser.add_argument("--max-hz", type=float, default=5.0)
    parser.add_argument("--demo", action="store_true", help="Explicit synthetic scene; never uses ROS")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 0.5 <= args.max_hz <= 10:
        parser.error("port must be 1..65535 and max-hz must be 0.5..10")
    stop = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    bridge = MapBridge(demo=args.demo)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((args.bind, args.port))
        listener.listen(4)
        print("mapping bridge listening on {}:{} ({})".format(args.bind, args.port,
              "SYNTHETIC DEMO" if args.demo else "read-only ROS; waiting for source"), flush=True)
        threading.Thread(target=serve, args=(bridge, listener, stop), daemon=True,
                         name="mapping-server").start()
        worker = demo_worker if args.demo else ros_worker
        arguments = (bridge, stop) if args.demo else (bridge, stop, args.max_hz)
        threading.Thread(target=worker, args=arguments, daemon=True,
                         name="mapping-source").start()
        try:
            while not stop.wait(1):
                bridge.heartbeat()
        finally:
            stop.set()
            bridge.close()


if __name__ == "__main__":
    main()
