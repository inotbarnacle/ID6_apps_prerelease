"""Verify the full-screen Shift menus bind to the production settings actions."""
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]/'fullscreen/native/generated'


class FullShiftResources(unittest.TestCase):
    def test_settings_gasoline_picker_and_existing_units_actions(self):
        for folder in ('','layouts/800x480/','layouts/960x480/'):
            tree=ET.parse(ROOT/(folder+'ID6FullShiftDeck.xml'))
            settings=tree.find(".//hmiState[@id='216']")
            picker=tree.find(".//hmiState[@id='219']")
            self.assertEqual(picker.get('parentId'),'216')
            self.assertEqual(tree.find(".//hmiAction[@id='10']").get('target'),'201')
            self.assertIsNotNone(tree.find(".//hmiState[@id='201']/optionComponents/button[@action='5751']"))
            expected={5800,5801,5810,5811,5812,5820,5821}
            self.assertEqual({int(n.get('action')) for n in settings.findall('components/button')
                              if n.get('action')!='219'},expected)
            for action in expected: self.assertIsNotNone(tree.find(".//actions/raAction[@id='%d']"%action))
            buttons=picker.findall('components/button')
            self.assertEqual(len(buttons),21)
            for index,node in enumerate(buttons):
                self.assertEqual(node.get('id'),str(3700+index))
                self.assertEqual(node.get('text'),f'{6000+index*100:,} RPM')
                combined=tree.find(".//combinedAction[@id='%d']/actions"%(4700+index))
                self.assertEqual(combined.find('raAction').get('id'),str(5700+index))
                self.assertEqual(combined.find('hmiAction').get('target'),'201')

    def test_profile_target_save_feedback_and_fallback_bounds(self):
        for folder,right in (('',1260),('layouts/800x480/',720),('layouts/960x480/',900)):
            tree=ET.parse(ROOT/(folder+'ID6FullShiftDeck.xml'))
            state=tree.find(".//hmiState[@id='201']")
            for model in range(1883,1887):
                self.assertIsNotNone(tree.find(".//models/raDataModel[@id='%d']"%model))
                self.assertIsNotNone(tree.find(".//hmiState[@id='216']/components/label[@model='%d']"%model))
            for model in (1883,1884):
                self.assertIsNotNone(state.find("components/label[@model='%d']"%model))
            self.assertIn('vehicle guidance',state.find("components/label[@id='1896']").get('text'))
            self.assertEqual(state.find("components/label[@id='1896']").get('model'),'1886')
            for node in state.findall('components/*'):
                properties={p.get('id'):int(p.get('value')) for p in node.findall('properties/property')}
                self.assertLessEqual(properties.get('20',0)+properties.get('9',0),right)
                self.assertLessEqual(properties.get('21',0)+properties.get('10',0),477)
            for selector in ('.//models/*','.//actions/*'):
                ids=[node.get('id') for node in tree.findall(selector)]
                self.assertEqual(len(ids),len(set(ids)))


if __name__=='__main__': unittest.main()
