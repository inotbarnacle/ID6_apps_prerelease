"""Run existing transport oracles against the actual full-screen worker builds."""
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import test_diesel_capabilities as capabilities
import test_egt_transport as transport
import test_egr_transport as egr
import test_pressure_transport as pressure
import test_reagent_transport as reagent
import test_warm_freshness as warm
from udp_test_socket import telemetry_socket

BUILD=Path(__file__).resolve().parents[1]/'build'


class FullBoostWorker(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(prefix='full-screen-workers-')
        self.addCleanup(self.directory.cleanup)
        self.binaries=Path(self.directory.name)
        for name,target in (('boost-deck','full-boost-deck'),('warmdeck-collector','full-warm-collector')):
            (self.binaries/name).symlink_to(BUILD/target)
        for module in (capabilities,transport,warm):
            old=module.BUILD;self.addCleanup(setattr,module,'BUILD',old);module.BUILD=self.binaries

    def test_automatic_capabilities_and_measurements_use_the_bound_activity_source(self):
        owner=self;peers=[];workers=[]
        original_popen=subprocess.Popen
        def launch(*args,**kwargs):
            process=original_popen(*args,**kwargs);workers.append(process.pid);return process

        class Receiver:
            def __init__(self): self.socket=telemetry_socket()
            def __getattr__(self,name): return getattr(self.socket,name)
            def __enter__(self): return self
            def __exit__(self,*args): self.socket.close()
            def recv(self,count):
                data,peer=self.socket.recvfrom(count)
                owner.assertEqual(peer,('127.0.0.1',42871));peers.append(peer)
                owner.assertEqual(int(data.split()[2]),workers[-1]);return data

        with patch.object(capabilities,'telemetry_socket',Receiver), patch.object(subprocess,'Popen',launch):
            capabilities.DieselCapabilitiesTest().test_automatic_report_matches_ecu_advertisement_and_valid_transmission()
        self.assertEqual(len(peers),80)

    def test_extended_selections_retain_the_existing_response_and_rate_oracles(self):
        transport.EgtTransportTest().test_two_banks_share_reads_and_preserve_high_sensor_bits()
        egr.EgrTransportTest().test_grouped_fields_and_legacy_sources_are_separate_and_rate_bounded()
        pressure.PressureTransportTest().test_grouped_pressure_units_and_rates()
        reagent.ReagentTransportTest().test_independent_fields_share_one_advertised_read_with_exact_units_and_period()

    def test_gasoline_keeps_legacy_queries_and_packet_bytes(self):
        transport.EgtTransportTest().test_gasoline_legacy_packet_and_fixed_rail_remain_unchanged()

    def test_most_extended_fields_and_ownership_use_the_fullscreen_build(self):
        transport.EgtTransportTest().test_most_decodes_multisensor_payloads_and_cleans_up()
        reagent.ReagentTransportTest().test_most_uses_the_same_advertised_fields_and_releases_ownership()

    def test_warm_oil_gate_and_request_age_use_the_fullscreen_build(self):
        capabilities.DieselCapabilitiesTest().test_warmdeck_keeps_coolant_and_transmission_without_oil()
        capabilities.DieselCapabilitiesTest().test_warmdeck_advertised_oil_and_exact_bitmap_chain()
        warm.WarmFreshnessTest().test_delayed_replies_keep_request_age_for_each_temperature()

    def test_occupied_activity_source_fails_before_any_gateway_read(self):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as marker:
            marker.bind(('127.0.0.1',42871))
            with capabilities.ecu_server({}) as (port,requests):
                result=subprocess.run([str(BUILD/'full-boost-deck'),'--host','127.0.0.1',
                    '--transport','hsfz','--port',str(port),'--cycles','1'],capture_output=True,timeout=3)
                self.assertEqual(result.returncode,1);self.assertEqual(requests,[])


if __name__=='__main__': unittest.main()
