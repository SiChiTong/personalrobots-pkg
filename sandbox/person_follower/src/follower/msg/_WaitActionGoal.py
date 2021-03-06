# autogenerated by genmsg_py from WaitActionGoal.msg. Do not edit.
import roslib.message
import struct

## \htmlinclude WaitActionGoal.msg.html

class WaitActionGoal(roslib.message.Message):
  _md5sum = "54f5dc6d242ed96aa3e20c82006143e4"
  _type = "follower/WaitActionGoal"
  _has_header = False #flag to mark the presence of a Header object
  _full_text = """int32 num_events
string topic_name

"""
  __slots__ = ['num_events','topic_name']
  _slot_types = ['int32','string']

  ## Constructor. Any message fields that are implicitly/explicitly
  ## set to None will be assigned a default value. The recommend
  ## use is keyword arguments as this is more robust to future message
  ## changes.  You cannot mix in-order arguments and keyword arguments.
  ##
  ## The available fields are:
  ##   num_events,topic_name
  ##
  ## @param args: complete set of field values, in .msg order
  ## @param kwds: use keyword arguments corresponding to message field names
  ## to set specific fields. 
  def __init__(self, *args, **kwds):
    super(WaitActionGoal, self).__init__(*args, **kwds)
    #message fields cannot be None, assign default values for those that are
    if self.num_events is None:
      self.num_events = 0
    if self.topic_name is None:
      self.topic_name = ''

  ## internal API method
  def _get_types(self): return WaitActionGoal._slot_types

  ## serialize message into buffer
  ## @param buff StringIO: buffer
  def serialize(self, buff):
    try:
      buff.write(struct.pack('<i', self.num_events))
      length = len(self.topic_name)
      #serialize self.topic_name
      buff.write(struct.pack('<I%ss'%length, length, self.topic_name))
    except struct.error, se: self._check_types(se)
    except TypeError, te: self._check_types(te)

  ## unpack serialized message in str into this message instance
  ## @param str str: byte array of serialized message
  def deserialize(self, str):
    try:
      end = 0
      start = end
      end += 4
      (self.num_events,) = struct.unpack('<i',str[start:end])
      start = end
      end += 4
      (length,) = struct.unpack('<I',str[start:end])
      #deserialize self.topic_name
      pattern = '<%ss'%length
      start = end
      end += struct.calcsize(pattern)
      (self.topic_name,) = struct.unpack(pattern, str[start:end])
      return self
    except struct.error, e:
      raise roslib.message.DeserializationError(e) #most likely buffer underfill

