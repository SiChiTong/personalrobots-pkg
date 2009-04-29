#!/usr/bin/env python
#
# Software License Agreement (BSD License)
#
# Copyright (c) 2008, Willow Garage, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above
#    copyright notice, this list of conditions and the following
#    disclaimer in the documentation and/or other materials provided
#    with the distribution.
#  * Neither the name of the Willow Garage nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

### Author: Kevin Watts, Josh Faust

import roslib
import roslib.packages
roslib.load_manifest('qualification')

import rospy
import roslaunch
import roslaunch.pmon

import os
import sys
import datetime
import glob
import wx
import time
from wx import xrc
from wx import html

import thread
from xml.dom import minidom

from qualification.msg import *
from srv import *
from test import *
from result import *
from config import * # Is this component thing going to work in a separate file?

from cStringIO import StringIO
import struct

from invent_client import Invent

TESTS_DIR = os.path.join(roslib.packages.get_pkg_dir('qualification'), 'tests')
RESULTS_DIR = os.path.join(roslib.packages.get_pkg_dir('qualification'), 'results')
ONBOARD_DIR = os.path.join(roslib.packages.get_pkg_dir('qualification'), 'onboard')
CONFIG_DIR = os.path.join(roslib.packages.get_pkg_dir('qualification'), 'config')


class SerialPanel(wx.Panel):
  def __init__(self, parent, resource, qualification_frame):
    wx.Panel.__init__(self, parent)
    
    self._manager = qualification_frame
    
    self._panel = resource.LoadPanel(self, 'serial_panel')
    self._sizer = wx.BoxSizer(wx.HORIZONTAL)
    
    self._sizer.Add(self._panel, 1, wx.EXPAND|wx.ALIGN_CENTER_VERTICAL)
    self.SetSizer(self._sizer)
    self.Layout()
    self.Fit()

    # Test cart tab
    self._test_button = xrc.XRCCTRL(self._panel, 'test_button')
    self._serial_text = xrc.XRCCTRL(self._panel, 'serial_text')
    self._rework_reason = xrc.XRCCTRL(self._panel, 'rework_reason')
    
    self._test_button.Bind(wx.EVT_BUTTON, self.on_test)
        
    # Config tab
    self._serial_text_conf = xrc.XRCCTRL(self._panel, 'serial_text_conf')

    self._config_button = xrc.XRCCTRL(self._panel, 'config_button')
    self._config_button.Bind(wx.EVT_BUTTON, self.on_config)

   # Onboards tab
    self._tree_ctrl = xrc.XRCCTRL(self._panel, 'test_tree_ctrl')

    root_name = self._manager._onboard_root_name
    root = self._tree_ctrl.AddRoot(root_name)
    self._root_node = root
        
    # Onboards are stored as a dictionary tree
    # Nodes with no children have values None for their keys
    self.insert_onboard_test_nodes(root, self._manager._onboards)
    self._tree_ctrl.Expand(root)
    
    self._robot_box = xrc.XRCCTRL(self._panel, 'robot_list_box')
    self._robot_box.InsertItems(dict.keys(self._manager._robots), 0)

    self._robot_qual_button = xrc.XRCCTRL(self._panel, 'robot_qual_button')
    self._robot_qual_button.Bind(wx.EVT_BUTTON, self.on_onboard_test)

    self._show_viz_box = xrc.XRCCTRL(self._panel, 'visualizer_checkbox')
    self._show_results_box = xrc.XRCCTRL(self._panel, 'show_results_checkbox')

    self._panel.Bind(wx.EVT_CHAR, self.on_char)
    self._panel.SetFocus()

  def on_config(self, event):
    # Get selected launch file
    serial = self._serial_text_conf.GetValue()

    if not self._manager.has_conf_script(serial):
      return

    launch_script = self._manager.select_conf_to_load(serial)
    name = self._manager._config_descrips_by_file[launch_script]

    wx.CallAfter(self._manager.configure_assembly, serial, name, launch_script)


  # Recursively add onboard test tree to tree control
  def insert_onboard_test_nodes(self, parent_node, branch):
    for test_label in branch.keys():
      # List of tests for each node
      tests = self._manager._onboards_by_label[test_label]

      # Is this used? Tests are found by label!
      # Store test string as part of item data on tree
      itemData = wx.TreeItemData(tests)
      tree_id = self._tree_ctrl.AppendItem(parent_node, test_label, data=itemData)
      
      # Expand first layer of tree
      if parent_node == self._root_node:
        self._tree_ctrl.Expand(tree_id)

      #self._tree_item_labels_by_id[tree_id] = test_label
      if branch[test_label] is not None:
        self.insert_onboard_test_nodes(tree_id, branch[test_label])

  # Need to get story straight on how onboards are stored in tree
  def on_onboard_test(self, event):
    try:
      # Add robot startup file to test list
      robot = self._robot_box.GetStringSelection()
      if robot is None or robot == '':
        return
      
      robot_launch = self._manager._robots[robot]
      
      # Get selections returns tree items that haven't been added
      selections = self._tree_ctrl.GetSelections() # returns wxTreeItemIds
      
      test_list = [robot_launch] # Add robot startup script to tests
      
      if len(selections) < 1:
        return

      for item in selections:
        label = self._tree_ctrl.GetItemText(item)
        # Recover onboard tests for that label
        tests = self._manager._onboards_by_label[label] 
        for tst in tests:
          test_list.append(tst)
        
      show_viz = self._show_viz_box.IsChecked()
      show_results = self._show_results_box.IsChecked()
      
      wx.CallAfter(self._manager.load_onboard_tests, test_list, robot, show_viz, show_results) 
    except Exception, e:
      print e
      wx.MessageBox('Unable to load onboard test for that selection.','Error - Invalid test selection', wx.OK|wx.ICON_ERROR, self)

  def on_test(self, event):
    # Get the first 7 characters of the serial
    serial = self._serial_text.GetValue()
    
    rework = self._rework_reason.GetValue()

    if (self._manager.has_test(serial)):
      if (len(rework) > 3):
        wx.CallAfter(self._manager.load_tests, serial, rework)
      else:
        wx.CallAfter(self._manager.load_tests, serial)
    else:
      wx.MessageBox('No test defined for that serial number.','Error - Invalid serial number', wx.OK|wx.ICON_ERROR, self)
  
  def on_char(self, event):
    # 347 is the keycode sent at the beginning of a barcode
    if (event.GetKeyCode() == 347):
      # Clear the old contents and put focus in the serial box so the rest of input goes there
      self._serial_text.Clear()
      self._serial_text_conf.Clear()
      # self._serial_text.SetFocus()
      
class InstructionsPanel(wx.Panel):
  def __init__(self, parent, resource, qualification_frame, file):
    wx.Panel.__init__(self, parent)
    
    self._manager = qualification_frame
    
    self._panel = resource.LoadPanel(self, 'instructions_panel')
    self._sizer = wx.BoxSizer(wx.HORIZONTAL)
    
    self._html_window = xrc.XRCCTRL(self._panel, 'html_window')
    self._continue_button = xrc.XRCCTRL(self._panel, 'continue_button')
    self._html_window.LoadFile(file)
    
    self._continue_button.Bind(wx.EVT_BUTTON, self.on_continue)
    self._continue_button.SetFocus()
    
    self._sizer.Add(self._panel, 1, wx.EXPAND|wx.ALIGN_CENTER_VERTICAL)
    self.SetSizer(self._sizer)
    self.Layout()
    self.Fit()
    
    self._cancel_button = xrc.XRCCTRL(self._panel, 'cancel_button')
    self._cancel_button.Bind(wx.EVT_BUTTON, self.on_cancel)
    
  def on_continue(self, event):
    self._manager.test_startup()
    
  def on_cancel(self, event):
    self._manager.cancel("Cancel button pressed")
    
class WaitingPanel(wx.Panel):
  def __init__(self, parent, resource, qualification_frame):
    wx.Panel.__init__(self, parent)
    
    self._manager = qualification_frame
    
    self._panel = resource.LoadPanel(self, 'waiting_panel')
    self._sizer = wx.BoxSizer(wx.HORIZONTAL)
    
    self._progress_label = xrc.XRCCTRL(self._panel, 'progress_label')
        
    self._sizer.Add(self._panel, 1, wx.EXPAND|wx.ALIGN_CENTER_VERTICAL)
    self.SetSizer(self._sizer)
    self.Layout()
    self.Fit()
    
    self._cancel_button = xrc.XRCCTRL(self._panel, 'cancel_button')
    self._cancel_button.Bind(wx.EVT_BUTTON, self.on_cancel)

  def set_progress_label_waiting(self, name, num, total, next = ''):
    label = "Test '%s' in progress...\nTest %d of %d." % (name, num, total)
    if next != '':
      label += "\nNext test: %s." % next
    self._progress_label.SetLabel(label)
    
  def set_progress_label(self, label):
    self._progress_label.SetLabel(label)

  def on_cancel(self, event):
    self._manager.cancel("Cancel button pressed during test.")
    

    
class ImagePanel(wx.Panel):
  def __init__(self, parent, id, image_data):
    wx.Panel.__init__(self, parent)
    
    wx.InitAllImageHandlers();
    
    stream = StringIO(image_data)
    self._image = wx.ImageFromStream(stream)
    self.scale_bitmap()
    
    self.Bind(wx.EVT_PAINT, self.on_paint)
    self.Bind(wx.EVT_ERASE_BACKGROUND, self.on_erase_background)
    self.Bind(wx.EVT_SIZE, self.on_size)
    
    self.SetSize(wx.Size(self._image.GetWidth(), self._image.GetHeight()))
    
    self._needs_scale = False
    
  def scale_bitmap(self):
    size = self.GetSize()
    image = self._image.Scale(size.GetWidth(), size.GetHeight())
    self._bitmap = wx.BitmapFromImage(image)

  def on_size(self, event):
    event.Skip()
    self._needs_scale = True
    self.Refresh()
    
  def on_paint(self, event):
    dc = wx.BufferedPaintDC(self)
    if (self._needs_scale):
      self.scale_bitmap()
      self._needs_scale = False
    dc.DrawBitmap(self._bitmap, 0, 0, False)
  
  def on_erase_background(self, event):
    pass
    
class PlotsPanel(wx.Panel):
  def __init__(self, parent, resource, qualification_frame, test_name):
    wx.Panel.__init__(self, parent)
    
    self._manager = qualification_frame
    
    self._panel = resource.LoadPanel(self, 'plots_panel')
    self._sizer = wx.BoxSizer(wx.HORIZONTAL)
    
    self._sizer.Add(self._panel, 1, wx.EXPAND)
    self.SetSizer(self._sizer)
    self.Layout()
    
    self._plots_window = xrc.XRCCTRL(self._panel, 'plots_window')
    self._pass_button = xrc.XRCCTRL(self._panel, 'pass_button')
    self._fail_button = xrc.XRCCTRL(self._panel, 'fail_button')
    self._retry_button = xrc.XRCCTRL(self._panel, 'retry_button')
    
    self._pass_button.Bind(wx.EVT_BUTTON, self.on_pass)
    self._fail_button.Bind(wx.EVT_BUTTON, self.on_fail)
    self._retry_button.Bind(wx.EVT_BUTTON, self.on_retry)
 
    
    self._pass_button.SetFocus()
    
    self._cancel_button = xrc.XRCCTRL(self._panel, 'cancel_button')
    self._cancel_button.Bind(wx.EVT_BUTTON, self.on_cancel)
    
  def show_plots(self, result_page):
     self._plots_window.SetPage(result_page)
    
  def on_pass(self, event):
    self._manager.subtest_result(True)
  
  def on_fail(self, event):
    failure_reason = wx.GetTextFromUser("Record the reason for failure:", "Failure Reason", "", self)
    self._manager.subtest_result(False, failure_reason)
  
  def on_retry(self, event):
    self._manager.retry_subtest()
    
  def on_cancel(self, event):
    self._manager.cancel("Cancel button pressed.")
    
class ResultsPanel(wx.Panel):
  def __init__(self, parent, resource, qualification_frame):
    wx.Panel.__init__(self, parent)
    
    self._manager = qualification_frame
    
    self._panel = resource.LoadPanel(self, 'results_panel')
    self._sizer = wx.BoxSizer(wx.HORIZONTAL)
    
    self._sizer.Add(self._panel, 1, wx.EXPAND|wx.ALIGN_CENTER_VERTICAL)
    self.SetSizer(self._sizer)
    self.Layout()
    self.Fit()
    
    self._submit_button = xrc.XRCCTRL(self._panel, 'submit_button')
    self._results_window = xrc.XRCCTRL(self._panel, 'results_window')

    self._notesbox = xrc.XRCCTRL(self._panel, 'notes_text')
    self._submit_button.Bind(wx.EVT_BUTTON, self.on_submit)
    self._submit_button.SetFocus()
    
  def set_results(self, results):
    self._results_window.Freeze()

    self._results_window.SetPage(results.make_summary_page())
    
    self._results_window.Thaw()


  def on_submit(self, event):
      wx.CallAfter(self._manager.submit_results, self._notesbox.GetValue())

class RoslaunchProcessListener(roslaunch.pmon.ProcessListener):
  def __init__(self):
    self._died_badly = False
    
  def has_any_process_died_badly(self):
    return self._died_badly

  def process_died(self, process_name, exit_code):
    status = 'Failed!'
    if exit_code == 0 or exit_code == None:
      status = 'OK'

    print "Process %s died with exit code %s. %s" % (process_name, exit_code, status)
    if exit_code != None and exit_code != 0:
      self._died_badly = True


class QualificationFrame(wx.Frame):
  def __init__(self, parent):
    wx.Frame.__init__(self, parent, wx.ID_ANY, "Qualification")

    self._result_service = None
    
    # Load test directory
    tests_xml_path = os.path.join(TESTS_DIR, 'tests.xml')
    self._tests = {}
    try:
      doc = minidom.parse(tests_xml_path)
    except IOError:
      print >> sys.stderr, "Could not load tests description from '%s'"%(tests_xml_path)
      sys.exit()

    self._test_descripts_by_file = {}

    # Loads tests by serial number of part
    tests = doc.getElementsByTagName('test')
    for test in tests:
      serial = test.attributes['serial'].value
      test_file = test.attributes['file'].value
      descrip = test.attributes['descrip'].value
      if self._tests.has_key(serial):
        self._tests[serial].append(test_file)
      else:
        self._tests[serial] = [ test_file ]
      
      self._test_descripts_by_file[test_file] = descrip

     

    # Store robots by name and launch file
    self._robots = { 'PRE' : '<startup>robots/pre_test.launch</startup>\n', 
                     'PRF' : '<startup>robots/prf_test.launch</startup>\n', 
                     'PRG' : '<startup>robots/prg_test.launch</startup>\n'}

    # Load onboard tests directory
    onboard_xml_path = os.path.join(ONBOARD_DIR, 'onboards.xml')
    self._onboards = {}
    try:
      doc = minidom.parse(onboard_xml_path)
    except IOError:
      print >> sys.stderr, "Could not load onboard description from '%s'"%(onboard_xml_path)
      sys.exit()
    
    self._onboard_root_name = 'Robot Onboard Test'
    self._onboards_by_label = {}
    self._onboards_by_label[self._onboard_root_name] = []
    
    self.find_onboard_test_nodes_from_xml(self._onboards, doc, 'robot', [self._onboard_root_name])

    # Load part configuration scripts
    config_xml_path = os.path.join(CONFIG_DIR, 'configs.xml')
    self._configs = {}
    try:
      doc = minidom.parse(config_xml_path)
    except IOError:
      print >> sys.stderr, "Could not load configuation scripts from '%s'"%(config_xml_path)
      sys.exit()
    
    self._config_descrips_by_file = {}
    configs = doc.getElementsByTagName('config')
    for conf in configs:
      serial = conf.attributes['serial'].value
      file = conf.attributes['file'].value
      descrip = conf.attributes['descrip'].value

      if self._configs.has_key(serial):
        self._configs[serial].append(file)
      else:
        self._configs[serial] = [ file ]

      self._config_descrips_by_file[file] = descrip

    # Load the XRC resource
    xrc_path = os.path.join(roslib.packages.get_pkg_dir('qualification'), 'xrc/gui.xrc')
    self._res = xrc.XmlResource(xrc_path)

    # Load the main panel
    self._root_panel = self._res.LoadPanel(self, 'main_panel')

    #self._log = xrc.XRCCTRL(self._root_panel, "log")
    self._top_panel = xrc.XRCCTRL(self._root_panel, "top_panel")
    self._top_sizer = wx.BoxSizer(wx.HORIZONTAL)
    self._top_panel.SetSizer(self._top_sizer)
    self._log_panel = xrc.XRCCTRL(self._root_panel, "log_panel")
    self._log = xrc.XRCCTRL(self._log_panel, 'log')
    
    self.reset()
    self.log("Startup")
    
    self.Bind(wx.EVT_CLOSE, self.on_close)
    
    self._startup_launch = None
    self._shutdown_launch = None
    self._core_launch = None
    self._subtest_launch = None
    
    self._spin_timer = wx.Timer(self, wx.ID_ANY)
    self.Bind(wx.EVT_TIMER, self.on_spin, self._spin_timer)
    self._spin_timer.Start(100)
    
    self._show_results_always = False
    self._config_service = None

    self._username = None
    self._password = None

  # Configure assembly
  # Load by serial # and log that config occured
  def configure_assembly(self, serial, name, script):
    self.log('Configuring subassembly %s as %s' % (serial, name) )

    wx.MessageBox('Make sure your power and etherCAT cables are connected.')
    
    self._core_launch = self.launch_core()
        
    self.log('Turning on power board')
    power_board_launch = self.launch_script(os.path.join(CONFIG_DIR, '../scripts/power_cycle.launch'), None)
    if power_board_launch == None:
      self.log('Unable to cycle power board! Cannot configure MCB!')
      return
    
    power_board_launch.spin()
        
    # Launch configuration script
    self.log('Starting configuration')
    listener = RoslaunchProcessListener()
    config_launcher = self.launch_script(os.path.join(CONFIG_DIR, script), listener)
  
    if config_launcher is None:
      self.log('Configuration failed, unable to load launch script %s' % launch_script)
      return

    config_launcher.spin()

    if (listener.has_any_process_died_badly()):
      s = 'Configuration failed! Press OK to cancel test.' % (script)
      wx.MessageBox(s, 'Configuration Failed', wx.OK|wx.ICON_ERROR, self)
      self.log('Configuration of %s failed!' % name)
      return

    self.log('Shutting down power board')
    shutdown_launch = self.launch_script(os.path.join(CONFIG_DIR, '../scripts/power_board_disable.launch'), None)
    if (shutdown_launch == None):
      s = 'Could not load roslaunch shutdown script. SHUTDOWN POWER BOARD MANUALLY!'
      wx.MessageBox(s, 'Invalid shutdown script!', wx.OK|wx.ICON_ERROR, self)
      self.log('No shutdown script')
      self.log('SHUT DOWN POWER BOARD MANUALLY')

    shutdown_launch.spin()
    
    shutdown_launch.stop()
    config_launcher.stop()
    power_board_launch.stop()

    self._core_launch.stop()
    self._core_launch = None
    shutdown_launch = None
    config_launcher = None
    power_board_launch = None
    self.log('Configuration finished')
    
    #res, msg = configure_component(script)
    
    #if res:
    msg = 'Configured part %s as %s.' % (serial, name)
    invent = self.get_inventory_object()
    if (invent != None):
      invent.setNote(serial, msg)

    wx.MessageBox(msg, 'Configuration complete', wx.OK, self)
    

  # TODO: This is dumb, just make subtest instead
  # Generates XMl line for each subtest, with pre and post tests
  def generate_subtest_xml_line(self, test, pre_test = None, post_test = None, name = None):
    details_str = ''
    if pre_test is not None:
      details_str += ' pre=\"%s\" ' % pre_test
    if post_test is not None:
      details_str += ' post=\"%s\" ' % post_test
    if name is not None:
      details_str += ' name=\"%s\" ' % name

    return '<subtest %s>%s</subtest>\n' % (details_str, test)
      
  # TODO: Could generate key for each test while parsing XML file!
  # Parses through the xml file recursively
  # For each element, finds the subtests associated with it, and makes and XML string for each subtest
  # Each subtest string is stored in a dictionary by label
  # Each label is a key to the XML for its test, and all child tests
  def find_onboard_test_nodes_from_xml(self, ob_branch, branch_xml_doc, parent_name, parent_labels):
    on_bds = branch_xml_doc.getElementsByTagName(parent_name)
    for node in on_bds:
      label = node.attributes['label'].value
      # Add the XML element for each test to each label and all parents
      if label not in dict.keys(self._onboards_by_label):
        self._onboards_by_label[label] = []

      # This test is associated with the parent labels and its own label
      test_labels = list(parent_labels)
      test_labels.append(label)

      node_keys = node.attributes.keys()

      test         = None
      pre_test     = None
      post_test    = None
      xml_for_test = None
      
      if 'pre' in node_keys:
        pre_test = node.attributes['pre'].value
      
      if 'post' in node_keys:
        post_test = node.attributes['post'].value

      # TODO: Store subtest as class, not as some weird XML thing
      if 'test' in node_keys:
        test = node.attributes['test'].value
        xml_for_test = self.generate_subtest_xml_line(test, pre_test, post_test, label)
        #sub_xml = SubTest(test, pre_test, post_test, label)
        # Add test XML to this test label, and all parent labels
        for lab in test_labels:
          self._onboards_by_label[lab].append(xml_for_test)
      
      if 'child_name' in node_keys:
        # Add child's tests to onboards tree
        ob_branch[label] = {}
        child_name = node.attributes['child_name'].value      
        self.find_onboard_test_nodes_from_xml(ob_branch[label], node, child_name, test_labels)
      else:
        ob_branch[label] = None # Means no child tests

  def log(self, msg):
    self._log.AppendText(datetime.now().strftime("%m/%d/%Y %H:%M:%S: ") + msg + '\n')
    self._log.Refresh()
    self._log.Update()
    
  def set_top_panel(self, panel):
    self._current_panel = panel
    self._top_sizer.Clear(True)
    self._top_sizer.Add(self._current_panel, 1, wx.EXPAND|wx.ALIGN_CENTER_VERTICAL)
    self._top_panel.Layout()
    
  def reset(self):
    self.set_top_panel(SerialPanel(self._top_panel, self._res, self))
    
  def launcher_spin_thread(self, launcher):
    launcher.spin()

  def has_conf_script(self, serial):
    return self._configs.has_key(serial[0:7])

  def has_test(self, serial):
    return self._tests.has_key(serial[0:7])
    
  def has_test_onboard(self, label):
    return self._onboards.has_key(label)

  # TODO: Combine in to a function to select using GUI and 
  # a function to pull the conf/test lists from serials numbers
  def select_conf_to_load(self, serial):
    short_serial = serial[0:7]
    
    # Load select_test_dialog
    configs_by_descrip = {}
    for conf in self._configs[short_serial]:
      configs_by_descrip[self._config_descrips_by_file[conf]] = conf
      
    # Load robot selection dialog
    dialog = self._res.LoadDialog(None, 'select_test_dialog')
    select_text = xrc.XRCCTRL(dialog, 'select_text')
    select_text.SetLabel('Select part of robot to configure component.')
    test_box = xrc.XRCCTRL(dialog, 'test_list_box')
    test_box.InsertItems(dict.keys(configs_by_descrip), 0)
    
    select_text.Wrap(250)
    
    dialog.Layout()
    dialog.Fit()

    # return string of test folder/file to run
    if (dialog.ShowModal() == wx.ID_OK):
      desc = test_box.GetStringSelection()
      
      return configs_by_descrip[desc]
    else: # User didn't want to configure, I guess
      return None

  # If more than one test for that serial, calls up prompt to ask user to select
  def select_test_to_load(self, short_serial):
    # Load select_test_dialog
    tests_by_descrip = {}
    for test in self._tests[short_serial]:
      tests_by_descrip[self._test_descripts_by_file[test]] = test
    
    # Load robot selection dialog
    dialog = self._res.LoadDialog(None, 'select_test_dialog')
    select_text = xrc.XRCCTRL(dialog, 'select_text')
    select_text.SetLabel('Select component or component type to qualify.')
    test_box = xrc.XRCCTRL(dialog, 'test_list_box')
    test_box.InsertItems(dict.keys(tests_by_descrip), 0)

    select_text.Wrap(250)
    
    dialog.Layout()
    dialog.Fit()

    # return string of test folder/file to run
    if (dialog.ShowModal() == wx.ID_OK):
      desc = test_box.GetStringSelection()
      
      return tests_by_descrip[desc]
    else: # User didn't want to test
      return None

  def load_onboard_tests(self, test_list, robot, show_viz, show_results):
    self._test_dir = ONBOARD_DIR
    self.log('Starting onboard test')
    self._current_serial = robot # Store robot as current part
    
    self._show_results_always = show_results

    # Make set of all tests -> avoid duplicates
    tests = set(test_list)

    # Instructions? -> later
    test_xml = '<test>\n' 

    if show_viz:
      test_xml = test_xml + '<subtest>visualization/visualization.launch</subtest>\n'
    for test in tests:
      test_xml = test_xml + str(test)
    test_xml = test_xml + '</test>'

     
    self._current_test = Test()
    self._current_test.load(test_xml)

    # Pass test class to qualification in future
    self.start_qualification()

  # Component qualification
  def load_tests(self, serial, rework_reason = ''):
    short_serial = serial[0:7]
    self.log('Starting test %s'%(self._tests[short_serial]))
    self._current_serial = serial

    if (len(rework_reason) > 0):
      invent = self.get_inventory_object()
      if (invent != None):
        invent.setNote(self._current_serial, "Hardware rework, reason given: %s"%(rework_reason))
     
    test_folder_file = self._tests[short_serial][0]
    if len(self._tests[short_serial]) > 1:
      # Load select_test_dialog to ask which test
      test_folder_file = self.select_test_to_load(short_serial)

    if test_folder_file is None:
      self.on_cancel()

    # Hack to find directory of scripts
    dir, sep, test_file = test_folder_file.partition('/')
    
    self._test_dir = os.path.join(TESTS_DIR, dir)
    test_str = open(os.path.join(TESTS_DIR, test_folder_file)).read()

    self._current_test = Test()
    self._current_test.load(test_str)
  
    self.start_qualification()
    
  # Launches program for either onboard or test cart tests
  def start_qualification(self): #, test_str):
    self._tests_start_date = datetime.now()
  
    self._results = QualTestResult(self._current_serial, self._tests_start_date)
    self._subtest = None
    
    # Print tests
    for st in self._current_test.subtests:
      print st.get_name()
    
    if (len(self._current_test.subtests) == 0):
      wx.MessageBox('Test selected has no subtests defined', 'No tests', wx.OK|wx.ICON_ERROR, self)
      return
    
    self._core_launch = self.launch_core()
    
    if (self._result_service != None):
      self._result_service.shutdown()
      self._result_service = None

    self._result_service = rospy.Service('test_result', TestResult, self.subtest_callback)

    if (self._current_test.getInstructionsFile() != None):
      self.set_top_panel(InstructionsPanel(self._top_panel, self._res, self, os.path.join(self._test_dir, self._current_test.getInstructionsFile())))
    else:
      self.test_startup()
     
  def test_startup(self):
    panel = WaitingPanel(self._top_panel, self._res, self)
    panel.set_progress_label('Initializing pre-startup sequence')
    self.set_top_panel(panel)
    time.sleep(2)
    wx.CallAfter(self.test_startup_real(panel))
    
  def run_prestartup_scripts(self, panel):
    # Run any pre_startup scripts synchronously
    if (len(self._current_test.pre_startup_scripts) > 0):
      for script in self._current_test.pre_startup_scripts:
        # Set script label in waiting panel
        log_message = 'Running pre_startup script [%s]...'%(script)
        self.log(log_message)
        panel.set_progress_label(log_message)
        
        # If cancel button pressed in pre-startup, cancel all tests
        # Cancel button called to cancel()...should work

        listener = RoslaunchProcessListener()
        
        launch = self.launch_script(os.path.join(self._test_dir, script), listener)
        if (launch == None):
          s = 'Could not load roslaunch script "%s"! Press OK to cancel test.' % (script)
          wx.MessageBox(s, 'Invalid roslaunch file', wx.OK|wx.ICON_ERROR, self)
          self.cancel(s)
          return
        
        launch.spin(); # Spin until it finishes
        
        if (listener.has_any_process_died_badly()):
          s = 'Pre_startup script %s died badly! Press OK to cancel test.' % (script)
          wx.MessageBox(s, 'Pre_startup Failed', wx.OK|wx.ICON_ERROR, self)
          # Record that pre-startup failed 
          self.prestartup_failed(script)
          
          return
    else:
      self.log('No pre_startup scripts')

  # Called from InstructionsPanel, runs through prestartup scripts
  def test_startup_real(self, panel):
    self.run_prestartup_scripts(panel)
      
    # Run the startup script if we have one
    if (self._current_test.getStartupScript() != None):
      self.log('Running startup script...')
      panel.set_progress_label('Running startup script')
      self._startup_launch = self.launch_script(os.path.join(self._test_dir, self._current_test.getStartupScript()), None)
      if (self._startup_launch == None):
        s = 'Could not load roslaunch script "%s"'%(self._current_test.getStartupScript())
        wx.MessageBox(s, 'Invalid roslaunch file. Press OK to cancel.', wx.OK|wx.ICON_ERROR, self)
        self.cancel(s)
        return
    else:
      self.log('No startup script')
      
    self.start_subtest(0)
    
  def start_subtest(self, index):
    self._subtest_index = index
    self._subtest = self._current_test.subtests[index]
    
    # Change label to make it cooler
    panel = WaitingPanel(self._top_panel, self._res, self)
    panel.set_progress_label_waiting(self._subtest.get_name(), index + 1, len(self._current_test.subtests))

    self.set_top_panel(panel)
    
    self.launch_pre_subtest()

    script = os.path.join(self._test_dir, self._subtest._test_script)
    self._subtest_launch = self.launch_script(script, None)
    if (self._subtest_launch == None):
      s = 'Could not load roslaunch script "%s"'%(script)
      wx.MessageBox(s, 'Invalid roslaunch file. Press OK to cancel.', wx.OK|wx.ICON_ERROR, self)
      self.cancel(s)
      return
    
  def prestartup_failed(self, script):
    msg = TestResult()
    msg.text_summary = 'Prestartup script %s failed.' % script
    msg.html_result = '<p>Prestartup script %s failed. Unable to load remainings subtests. Qualification canceled.</p>'
    msg.plots = None

    name = 'Pre-startup %s' % script
    self._results.add_subtest(-1, name, msg)

    self.test_finished()

    self.show_results()

  def show_results(self):
    panel = ResultsPanel(self._top_panel, self._res, self)
    self.set_top_panel(panel)
    
    self._results.write_results_to_file() # Write to temp dir

    panel.set_results(self._results)
    
  def get_inventory_object(self):
    if (self._username != None and self._password != None):
      invent = Invent(self._username, self._password)
      if (invent.login() == False):
        self._username = None
        self._password = None
      else:
        return invent
    
    dialog = self._res.LoadDialog(self, 'username_password_dialog')
    xrc.XRCCTRL(dialog, 'text').Wrap(300)
    dialog.Layout()
    dialog.Fit()
    username_ctrl = xrc.XRCCTRL(dialog, 'username')
    password_ctrl = xrc.XRCCTRL(dialog, 'password')
    username_ctrl.SetFocus()
    
    # These values don't come through in the xrc file
    username_ctrl.SetMinSize(wx.Size(200, -1))
    password_ctrl.SetMinSize(wx.Size(200, -1))
    if (dialog.ShowModal() == wx.ID_OK):
      username = username_ctrl.GetValue()
      password = password_ctrl.GetValue()

      invent = Invent(username, password)
      if (invent.login() == False):
        return self.get_inventory_object()
      
      self._username = username
      self._password = password
      
      return invent
    
    return None
    
  def submit_results(self, notes = ''):
    if self._current_serial is None:
      self.log('Can\'t submit, no serial number')
      self.reset()
      return

    invent = self.get_inventory_object()
    self._results.set_notes(notes)
    self._results.set_operator(self._username)
    
    self._results.log_results_invent(invent)

    #if (invent != None):
    #  status_str = self._results.test_status_str()
      
        #msg = r['msg']
        #if msg is not None:
        #  for p in msg.plots:
        #    if (len(p.image) > 0 ):
        #      invent.add_attachment(self._current_serial, prefix + p.title, "image/" + p.image_format, p.image)
        #      i += 1
            
    self.log('Results submitted in inventory system.')

    self._results.write_results_to_file(False)

    self.log('Results logged to %s' % self._results._results_dir)
    
    self.reset()

  def discard_results(self):
    self.log('Results discarded')
    self.reset()
    
  def next_subtest(self):
    if (self._subtest_index + 1 >= len(self._current_test.subtests)):
      self.test_finished()
      self.show_results()
    else:
      self.start_subtest(self._subtest_index + 1)
    
  def show_plots(self, sub_result):
    panel = PlotsPanel(self._top_panel, self._res, self, self._subtest._test_script)
    self.set_top_panel(panel)
    
    panel.show_plots(sub_result.make_result_page())
  
  def launch_pre_subtest(self):
    if (self._subtest == None):
      return
    
    if (self._subtest._pre_script is not None):
      script = os.path.join(self._test_dir, self._subtest._pre_script)
      pre_launcher = self.launch_script(script, None)
      if (pre_launcher == None):
        s = 'Could not load pre-subtest roslaunch script "%s"'%(script)
        wx.MessageBox(s, 'Invalid roslaunch file', wx.OK|wx.ICON_ERROR, self)
        self.cancel(s)
      else:
        pre_launcher.spin()      

  def launch_post_subtest(self):
    if (self._subtest == None):
      return
    
    if (self._subtest._post_script is not None):
      script = os.path.join(self._test_dir, self._subtest._post_script)
      post_launcher = self.launch_script(script, None)
      if (post_launcher == None):
        s = 'Could not load post-subtest roslaunch script "%s"'%(script)
        wx.MessageBox(s, 'Invalid roslaunch file', wx.OK|wx.ICON_ERROR, self)
        self.cancel(s)
      else:
        post_launcher.spin() 
  
  def subtest_result(self, result, failure_reason = ''):
    str_result = "PASS"
    if result:
      str_result = "FAIL"
    
    self.log('Subtest "%s" result: %s'%(self._subtest.get_name(), str_result))
    
    sub_result = self._results.get_subresult(self._subtest_index)
    sub_result.set_failure_text(failure_reason)
    sub_result.set_passfail(result)

    self.launch_post_subtest()
    
    if not result:
      self.test_finished() # Terminate rest of test
      self.show_results()
    else:
      self.next_subtest()
    
  def subtest_finished(self, msg):
    self.log('Subtest "%s" finished'%(self._subtest.get_name()))
    self._subtest_launch.stop()
    self._subtest_launch = None
    
    sub_result = self._results.add_sub_result(self._subtest_index, self._subtest.get_name(), msg)

    if self._show_results_always:
      self.show_plots(sub_result)
    else:
      if (msg.result == TestResultRequest.RESULT_PASS):
        self.subtest_result(True)
      elif (msg.result == TestResultRequest.RESULT_FAIL):
        self.subtest_result(False, "Automated failure")
      elif (msg.result == TestResultRequest.RESULT_HUMAN_REQUIRED):
        self.log('Subtest "%s" needs human response'%(self._subtest.get_name()))
        self.show_plots(sub_result)
    
  def subtest_callback(self, msg):
    wx.CallAfter(self.subtest_finished, msg)
    return TestResultResponse()
    
  def retry_subtest(self):
    self.log('Retrying subtest "%s"'%(self._subtest.get_name()))
    self.start_subtest(self._subtest_index)
    
  def launch_core(self):
    self.log('Launching ros core services')
    
    # Create a roslauncher
    config = roslaunch.ROSLaunchConfig()
    config.master.auto = config.master.AUTO_RESTART
    
    launcher = roslaunch.ROSLaunchRunner(config)
    launcher.launch()
    
    return launcher
    
  def launch_script(self, script, process_listener_object):
    self.log('Launching roslaunch file %s'%(script))
    
    # Create a roslauncher
    config = roslaunch.ROSLaunchConfig()

    # Try loading the XML file
    try:
      loader = roslaunch.XmlLoader()
      loader.load(script, config)
      
      # Bring up the nodes
      launcher = roslaunch.ROSLaunchRunner(config)
      if (process_listener_object != None):
        launcher.pm.add_process_listener(process_listener_object)
        
      launcher.launch()
    except roslaunch.RLException, e:
      self.log('Failed to launch roslaunch file %s: %s'%(script, e))
      return None
    
    return launcher
    
  def on_spin(self, event):
    if (self._subtest_launch != None):
      self._subtest_launch.spin_once()
    
    if (self._startup_launch != None):
      self._startup_launch.spin_once()
      
    if (self._shutdown_launch != None):
      self._shutdown_launch.spin_once()
      
    if (self._core_launch != None):
      self._core_launch.spin_once()
    
  def stop_launches(self):
    if (self._subtest_launch != None):
      self._subtest_launch.stop()
    
    if (self._startup_launch != None):
      self._startup_launch.stop()
      
    if (self._shutdown_launch != None):
      self._shutdown_launch.stop()
      
    if (self._core_launch != None):
      self._core_launch.stop()
      
    self._startup_launch = None
    self._shutdown_launch = None
    self._core_launch = None
    self._subtest_launch = None
    
  def test_finished(self):
    self.launch_post_subtest()
    
    if (self._current_test.getShutdownScript() != None):
      self.log('Running shutdown script...')
      self._shutdown_launch = self.launch_script(os.path.join(self._test_dir, self._current_test.getShutdownScript()), None)
      if (self._shutdown_launch == None):
        s = 'Could not load roslaunch shutdown script "%s". SHUTDOWN POWER BOARD MANUALLY!'%(self._current_test.getShutdownScript())
        wx.MessageBox(s, 'Invalid shutdown script!', wx.OK|wx.ICON_ERROR, self)
        self.log('No shutdown script: %s' % (s))
        self.log('SHUT DOWN POWER BOARD MANUALLY')
        self.reset()
        return

      self._shutdown_launch.spin()
    else:
      self.log('No shutdown script')
        
    self.stop_launches()
    
    self._current_test = None
    self._subtest = None
    self._subtest_index = -1
    
  def cancel(self, reason):
    self.log('Test canceled, reason: %s'%(reason))
    
    self.test_finished()
    self.reset()
  
  def on_close(self, event):
    event.Skip()
    
    self.stop_launches()
    

class QualificationApp(wx.App):
  def OnInit(self):
    rospy.init_node("Qualifier", anonymous=True, disable_rostime=True)
    
    self._frame = QualificationFrame(None)
    self._frame.SetSize(wx.Size(700,1000))
    self._frame.Layout()
    self._frame.Centre()
    self._frame.Show(True)
    
    return True

if __name__ == '__main__':
  try:
    app = QualificationApp(0)
    app.MainLoop()
  except Exception, e:
    print e
    
  print 'Quitting qualification app'

