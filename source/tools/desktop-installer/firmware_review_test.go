package main

import "testing"

func TestSupraFirmwareProfileMatchesReportedFrameworks(t *testing.T) {
	reported := frameworkPair{
		app:  "4ce83759b1f25175237dcab1c2c6bd63dd4d60f289274d57f345685aaad60721",
		core: "3e51c85105a807815ddec7aef943816534df0649cbdead6d1fd6ccb8b8c68630",
	}
	for _, version := range []string{"NBTevo_T23235J", "NBTevo_Y23236I"} {
		if got := profiles[version]; got != reported {
			t.Errorf("%s hashes = %+v, want %+v", version, got, reported)
		}
	}
}

func TestNewProfilesMatchExistingPairs(t *testing.T) {
	for newVersion, existing := range map[string]string{
		"K17052I": "M17345I", "M17345I": "M17365I", "R19101I": "R19144I", "S19221I": "S19322I", "T23235J": "Y23236I",
	} {
		got, present := profiles["NBTevo_"+newVersion]
		want, known := profiles["NBTevo_"+existing]
		if !present || !known || got != want {
			t.Errorf("%s and %s profiles differ", newVersion, existing)
		}
	}
}

func TestT24081IUsesOrdinaryY24073JFrameworkProfile(t *testing.T) {
	const version = "NBTevo_T24081I"
	pair, present := profiles[version]
	if !present || pair != profiles["NBTevo_Y24073J"] || captureOnly[version] {
		t.Fatal("expected an installable alias of the Y24073J framework pair")
	}
}
