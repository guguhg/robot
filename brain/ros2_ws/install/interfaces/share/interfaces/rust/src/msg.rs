#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};



// Corresponds to interfaces__msg__MotorCmd
/// 四轮电机控制指令 (r/s)

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct MotorCmd {

    // This member is not documented.
    #[allow(missing_docs)]
    pub left_front: f32,


    // This member is not documented.
    #[allow(missing_docs)]
    pub right_front: f32,


    // This member is not documented.
    #[allow(missing_docs)]
    pub left_rear: f32,


    // This member is not documented.
    #[allow(missing_docs)]
    pub right_rear: f32,

}



impl Default for MotorCmd {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::MotorCmd::default())
  }
}

impl rosidl_runtime_rs::Message for MotorCmd {
  type RmwMsg = super::msg::rmw::MotorCmd;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        left_front: msg.left_front,
        right_front: msg.right_front,
        left_rear: msg.left_rear,
        right_rear: msg.right_rear,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      left_front: msg.left_front,
      right_front: msg.right_front,
      left_rear: msg.left_rear,
      right_rear: msg.right_rear,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      left_front: msg.left_front,
      right_front: msg.right_front,
      left_rear: msg.left_rear,
      right_rear: msg.right_rear,
    }
  }
}


// Corresponds to interfaces__msg__MotorStates
/// 四轮电机速度反馈 (r/s)

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Clone, Debug, PartialEq, PartialOrd)]
pub struct MotorStates {

    // This member is not documented.
    #[allow(missing_docs)]
    pub left_front: f32,


    // This member is not documented.
    #[allow(missing_docs)]
    pub right_front: f32,


    // This member is not documented.
    #[allow(missing_docs)]
    pub left_rear: f32,


    // This member is not documented.
    #[allow(missing_docs)]
    pub right_rear: f32,

}



impl Default for MotorStates {
  fn default() -> Self {
    <Self as rosidl_runtime_rs::Message>::from_rmw_message(super::msg::rmw::MotorStates::default())
  }
}

impl rosidl_runtime_rs::Message for MotorStates {
  type RmwMsg = super::msg::rmw::MotorStates;

  fn into_rmw_message(msg_cow: std::borrow::Cow<'_, Self>) -> std::borrow::Cow<'_, Self::RmwMsg> {
    match msg_cow {
      std::borrow::Cow::Owned(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
        left_front: msg.left_front,
        right_front: msg.right_front,
        left_rear: msg.left_rear,
        right_rear: msg.right_rear,
      }),
      std::borrow::Cow::Borrowed(msg) => std::borrow::Cow::Owned(Self::RmwMsg {
      left_front: msg.left_front,
      right_front: msg.right_front,
      left_rear: msg.left_rear,
      right_rear: msg.right_rear,
      })
    }
  }

  fn from_rmw_message(msg: Self::RmwMsg) -> Self {
    Self {
      left_front: msg.left_front,
      right_front: msg.right_front,
      left_rear: msg.left_rear,
      right_rear: msg.right_rear,
    }
  }
}


