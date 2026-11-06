#!/usr/bin/env python3
"""Offline relay tests: every UDP endpoint is an ephemeral loopback socket."""
import importlib.util
import pathlib
import socket
import struct
import threading
import time
import unittest
from unittest import mock


MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "deploy" / "siyi_gimbal_relay.py"
SPEC = importlib.util.spec_from_file_location("siyi_gimbal_relay_under_test", MODULE_PATH)
relay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(relay)


class RecentClientsTest(unittest.TestCase):
    def test_ttl_limit_and_refresh_are_bounded(self):
        clients = relay.RecentClients(ttl=10., limit=8)
        addresses = [("127.0.0.1", 10000 + index) for index in range(9)]
        for index, client in enumerate(addresses):
            clients.touch(client, float(index))
        self.assertEqual(clients.active(8.), tuple(addresses[1:]));
        clients.touch(addresses[1], 9.)
        self.assertEqual(clients.active(12.), tuple(addresses[3:] + [addresses[1]]))
        # Receiving camera traffic does not renew subscriptions.
        self.assertEqual(clients.active(19.), ())

    def test_frame_extraction_validates_crc_and_recovers_bundles(self):
        attitude = relay.build_frame(0x0D, b"\0" * 12)
        feedback = relay.build_frame(0x0B, b"\0")
        corrupt = attitude[:-1] + bytes([attitude[-1] ^ 1])
        self.assertEqual(relay.valid_frames(b"junk" + attitude + corrupt + feedback), [attitude, feedback])
        self.assertEqual(relay.valid_frames(attitude[:-1]), [])


class RelaySocketTest(unittest.TestCase):
    def setUp(self):
        self.sockets = []
        self.camera = self.new_socket(timeout=.1)
        self.downstream = self.new_socket(timeout=None)
        self.upstream = self.new_socket(timeout=None)
        self.downstream.setblocking(False)
        self.upstream.setblocking(False)
        self.first = self.new_socket(timeout=.08)
        self.second = self.new_socket(timeout=.08)
        self.stop = threading.Event()
        self.thread = None
        self.thread_errors = []
        self.quiet = mock.patch.object(relay, "log", lambda message: None)
        self.quiet.start()

    def new_socket(self, timeout):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("127.0.0.1", 0))
        sock.settimeout(timeout)
        self.sockets.append(sock)
        return sock

    def start_relay(self, **options):
        def run():
            try:
                relay.run_udp_relay(self.downstream, self.upstream,
                                    camera_addr=self.camera.getsockname(), stop_event=self.stop,
                                    select_timeout=.005, **options)
            except Exception as error:
                self.thread_errors.append(error)
        self.thread = threading.Thread(target=run, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.stop.set()
        if self.thread is not None:
            self.thread.join(1.)
            self.assertFalse(self.thread.is_alive())
        for sock in self.sockets:
            sock.close()
        self.quiet.stop()
        self.assertEqual(self.thread_errors, [])

    def forward(self, client, packet):
        client.sendto(packet, self.downstream.getsockname())
        received, source = self.camera.recvfrom(4096)
        self.assertEqual(received, packet)
        return source

    def assert_no_packet(self, sock):
        with self.assertRaises(socket.timeout):
            sock.recvfrom(4096)

    def test_interleaved_clients_share_attitude_and_async_photo_feedback(self):
        self.start_relay()
        upstream_ports = set()
        # A photo command has no ACK; another client may poll before feedback arrives.
        for client, packet in (
                (self.first, relay.build_frame(0x0D)),
                (self.second, relay.build_frame(0x0A)),
                (self.first, relay.build_frame(0x0C, b"\0")),
                (self.second, relay.build_frame(0x0D))):
            source = self.forward(client, packet)
            upstream_ports.add(source[1])
        self.assertEqual(len(upstream_ports), 1)
        packet = relay.build_frame(0x0D, b"\0" * 12) + relay.build_frame(0x0B, b"\0")
        self.camera.sendto(packet, source)
        self.assertEqual(self.first.recvfrom(4096)[0], packet)
        self.assertEqual(self.second.recvfrom(4096)[0], packet)

    def test_only_exact_camera_source_and_crc_valid_frames_are_fanned_out(self):
        self.start_relay()
        source = self.forward(self.first, relay.build_frame(0x0D))
        self.forward(self.second, relay.build_frame(0x0A))
        stranger = self.new_socket(timeout=.08)  # Same IP, different source port.
        packet = relay.build_frame(0x0D, b"\0" * 12)
        stranger.sendto(packet, source)
        self.camera.sendto(packet[:-1] + bytes([packet[-1] ^ 1]), source)
        self.assert_no_packet(self.first)
        self.assert_no_packet(self.second)
        self.camera.sendto(packet, source)
        self.assertEqual(self.first.recvfrom(4096)[0], packet)
        self.assertEqual(self.second.recvfrom(4096)[0], packet)

    def test_embedded_heartbeat_filtered_and_invalid_client_not_subscribed(self):
        self.start_relay()
        query = relay.build_frame(0x0D)
        self.first.sendto(relay.build_frame(0x00) + query, self.downstream.getsockname())
        received, source = self.camera.recvfrom(4096)
        self.assertEqual(received, query)
        self.assert_no_packet(self.camera)
        self.second.sendto(b"invalid" + relay.build_frame(0x00), self.downstream.getsockname())
        self.assert_no_packet(self.camera)
        reply = relay.build_frame(0x0D, b"\0" * 12)
        self.camera.sendto(reply, source)
        self.assertEqual(self.first.recvfrom(4096)[0], reply)
        self.assert_no_packet(self.second)

    def test_expired_client_does_not_receive_async_camera_feedback(self):
        self.start_relay(client_ttl=.25)
        source = self.forward(self.first, relay.build_frame(0x0D))
        time.sleep(.18)
        self.forward(self.second, relay.build_frame(0x0D))
        time.sleep(.12)
        reply = relay.build_frame(0x0B, b"\0")
        self.camera.sendto(reply, source)
        self.assertEqual(self.second.recvfrom(4096)[0], reply)
        self.assert_no_packet(self.first)

    def test_other_clients_queries_cannot_extend_velocity_deadman(self):
        self.start_relay(rotate_deadman=.08)
        source = self.forward(self.first, relay.build_frame(0x07, struct.pack("bb", 30, -10)))
        stopped = []
        end = time.monotonic() + .22
        while time.monotonic() < end:
            self.second.sendto(relay.build_frame(0x0D), self.downstream.getsockname())
            try:
                packet, sender = self.camera.recvfrom(4096)
            except socket.timeout:
                continue
            self.assertEqual(sender, source)
            if relay.command_id(packet) == 0x07:
                stopped.append(packet)
            time.sleep(.01)
        self.assertEqual(stopped, [relay.build_frame(0x07, b"\0\0")])

    def test_explicit_zero_stop_is_forwarded_once_without_later_deadman_stop(self):
        self.start_relay(rotate_deadman=.05)
        source = self.forward(self.first, relay.build_frame(0x07, b"\x1e\0"))
        self.forward(self.second, relay.build_frame(0x0D))
        stopped_from = self.forward(self.first, relay.build_frame(0x07, b"\0\0"))
        self.assertEqual(stopped_from, source)
        time.sleep(.08)
        self.assert_no_packet(self.camera)


if __name__ == "__main__":
    unittest.main()
