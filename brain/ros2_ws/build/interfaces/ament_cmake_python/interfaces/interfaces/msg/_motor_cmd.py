# generated from rosidl_generator_py/resource/_idl.py.em
# with input from interfaces:msg/MotorCmd.idl
# generated code does not contain a copyright notice


# Import statements for member types

import builtins  # noqa: E402, I100

import math  # noqa: E402, I100

import rosidl_parser.definition  # noqa: E402, I100


class Metaclass_MotorCmd(type):
    """Metaclass of message 'MotorCmd'."""

    _CREATE_ROS_MESSAGE = None
    _CONVERT_FROM_PY = None
    _CONVERT_TO_PY = None
    _DESTROY_ROS_MESSAGE = None
    _TYPE_SUPPORT = None

    __constants = {
    }

    @classmethod
    def __import_type_support__(cls):
        try:
            from rosidl_generator_py import import_type_support
            module = import_type_support('interfaces')
        except ImportError:
            import logging
            import traceback
            logger = logging.getLogger(
                'interfaces.msg.MotorCmd')
            logger.debug(
                'Failed to import needed modules for type support:\n' +
                traceback.format_exc())
        else:
            cls._CREATE_ROS_MESSAGE = module.create_ros_message_msg__msg__motor_cmd
            cls._CONVERT_FROM_PY = module.convert_from_py_msg__msg__motor_cmd
            cls._CONVERT_TO_PY = module.convert_to_py_msg__msg__motor_cmd
            cls._TYPE_SUPPORT = module.type_support_msg__msg__motor_cmd
            cls._DESTROY_ROS_MESSAGE = module.destroy_ros_message_msg__msg__motor_cmd

    @classmethod
    def __prepare__(cls, name, bases, **kwargs):
        # list constant names here so that they appear in the help text of
        # the message class under "Data and other attributes defined here:"
        # as well as populate each message instance
        return {
        }


class MotorCmd(metaclass=Metaclass_MotorCmd):
    """Message class 'MotorCmd'."""

    __slots__ = [
        '_left_front',
        '_right_front',
        '_left_rear',
        '_right_rear',
    ]

    _fields_and_field_types = {
        'left_front': 'float',
        'right_front': 'float',
        'left_rear': 'float',
        'right_rear': 'float',
    }

    SLOT_TYPES = (
        rosidl_parser.definition.BasicType('float'),  # noqa: E501
        rosidl_parser.definition.BasicType('float'),  # noqa: E501
        rosidl_parser.definition.BasicType('float'),  # noqa: E501
        rosidl_parser.definition.BasicType('float'),  # noqa: E501
    )

    def __init__(self, **kwargs):
        assert all('_' + key in self.__slots__ for key in kwargs.keys()), \
            'Invalid arguments passed to constructor: %s' % \
            ', '.join(sorted(k for k in kwargs.keys() if '_' + k not in self.__slots__))
        self.left_front = kwargs.get('left_front', float())
        self.right_front = kwargs.get('right_front', float())
        self.left_rear = kwargs.get('left_rear', float())
        self.right_rear = kwargs.get('right_rear', float())

    def __repr__(self):
        typename = self.__class__.__module__.split('.')
        typename.pop()
        typename.append(self.__class__.__name__)
        args = []
        for s, t in zip(self.__slots__, self.SLOT_TYPES):
            field = getattr(self, s)
            fieldstr = repr(field)
            # We use Python array type for fields that can be directly stored
            # in them, and "normal" sequences for everything else.  If it is
            # a type that we store in an array, strip off the 'array' portion.
            if (
                isinstance(t, rosidl_parser.definition.AbstractSequence) and
                isinstance(t.value_type, rosidl_parser.definition.BasicType) and
                t.value_type.typename in ['float', 'double', 'int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64']
            ):
                if len(field) == 0:
                    fieldstr = '[]'
                else:
                    assert fieldstr.startswith('array(')
                    prefix = "array('X', "
                    suffix = ')'
                    fieldstr = fieldstr[len(prefix):-len(suffix)]
            args.append(s[1:] + '=' + fieldstr)
        return '%s(%s)' % ('.'.join(typename), ', '.join(args))

    def __eq__(self, other):
        if not isinstance(other, self.__class__):
            return False
        if self.left_front != other.left_front:
            return False
        if self.right_front != other.right_front:
            return False
        if self.left_rear != other.left_rear:
            return False
        if self.right_rear != other.right_rear:
            return False
        return True

    @classmethod
    def get_fields_and_field_types(cls):
        from copy import copy
        return copy(cls._fields_and_field_types)

    @builtins.property
    def left_front(self):
        """Message field 'left_front'."""
        return self._left_front

    @left_front.setter
    def left_front(self, value):
        if __debug__:
            assert \
                isinstance(value, float), \
                "The 'left_front' field must be of type 'float'"
            assert not (value < -3.402823466e+38 or value > 3.402823466e+38) or math.isinf(value), \
                "The 'left_front' field must be a float in [-3.402823466e+38, 3.402823466e+38]"
        self._left_front = value

    @builtins.property
    def right_front(self):
        """Message field 'right_front'."""
        return self._right_front

    @right_front.setter
    def right_front(self, value):
        if __debug__:
            assert \
                isinstance(value, float), \
                "The 'right_front' field must be of type 'float'"
            assert not (value < -3.402823466e+38 or value > 3.402823466e+38) or math.isinf(value), \
                "The 'right_front' field must be a float in [-3.402823466e+38, 3.402823466e+38]"
        self._right_front = value

    @builtins.property
    def left_rear(self):
        """Message field 'left_rear'."""
        return self._left_rear

    @left_rear.setter
    def left_rear(self, value):
        if __debug__:
            assert \
                isinstance(value, float), \
                "The 'left_rear' field must be of type 'float'"
            assert not (value < -3.402823466e+38 or value > 3.402823466e+38) or math.isinf(value), \
                "The 'left_rear' field must be a float in [-3.402823466e+38, 3.402823466e+38]"
        self._left_rear = value

    @builtins.property
    def right_rear(self):
        """Message field 'right_rear'."""
        return self._right_rear

    @right_rear.setter
    def right_rear(self, value):
        if __debug__:
            assert \
                isinstance(value, float), \
                "The 'right_rear' field must be of type 'float'"
            assert not (value < -3.402823466e+38 or value > 3.402823466e+38) or math.isinf(value), \
                "The 'right_rear' field must be a float in [-3.402823466e+38, 3.402823466e+38]"
        self._right_rear = value
