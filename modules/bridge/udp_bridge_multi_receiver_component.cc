/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/
#include "modules/bridge/udp_bridge_multi_receiver_component.h"
#include "modules/bridge/common/macro.h"
#include "modules/bridge/common/util.h"
#include "modules/bridge/common/bridge_proto_diser_buf_factory.h"

namespace apollo {
namespace bridge {

UDPBridgeMultiReceiverComponent::UDPBridgeMultiReceiverComponent()
    : monitor_logger_buffer_(common::monitor::MonitorMessageItem::CONTROL) {}

bool UDPBridgeMultiReceiverComponent::Init() {
  AINFO << "UDP bridge multi :receiver init, startin...";
  apollo::bridge::UDPBridgeReceiverRemoteInfo udp_bridge_remote;
  if (!this->GetProtoConfig(&udp_bridge_remote)) {
    AINFO << "load udp bridge component proto param failed";
    return false;
  }
  bind_port_ = udp_bridge_remote.bind_port();
  proto_name_ = udp_bridge_remote.proto_name();
  topic_name_ = udp_bridge_remote.topic_name();
  enable_timeout_ = udp_bridge_remote.enable_timeout();
  ADEBUG << "UDP Bridge remote port is: " << bind_port_;
  ADEBUG << "UDP Bridge for Proto is: " << proto_name_;

  if (!InitSession((uint16_t)bind_port_)) {
    return false;
  }
  ADEBUG << "initialize session successful.";
  MsgDispatcher();
  return true;
}

bool UDPBridgeMultiReceiverComponent::InitSession(uint16_t port) {
  return listener_->Initialize(this,
    &UDPBridgeMultiReceiverComponent::MsgHandle, port);
}

void UDPBridgeMultiReceiverComponent::MsgDispatcher() {
  ADEBUG << "msg dispatcher start successful.";
  listener_->Listen();
}

ProtoDiserializedBufBase
    *UDPBridgeMultiReceiverComponent::CreateBridgeProtoBuf(
        const BridgeHeader &header) {
  if (IsTimeout(header.GetTimeStamp())) {
    std::vector<ProtoDiserializedBufBase *>::iterator itor
      = proto_list_.begin();
    for (; itor != proto_list_.end();) {
      if ((*itor)->IsTheProto(header)) {
        ProtoDiserializedBufBase *tmp = *itor;
        FREE_POINTER(tmp);
        itor = proto_list_.erase(itor);
        break;
      }
      ++itor;
    }
    return nullptr;
  }

  for (auto proto : proto_list_) {
    if (proto->IsTheProto(header)) {
      return proto;
    }
  }

  ProtoDiserializedBufBase *proto_buf =
    ProtoDiserializedBufBaseFactory::CreateObj(header);
  if (!proto_buf) {
    return nullptr;
  }
  proto_buf->Initialize(header, node_, topic_name_);
  proto_list_.push_back(proto_buf);
  return proto_buf;
}

bool UDPBridgeMultiReceiverComponent::IsProtoExist(const BridgeHeader &header) {
  for (auto proto : proto_list_) {
    if (proto->IsTheProto(header)) {
      return true;
    }
  }
  return false;
}

bool UDPBridgeMultiReceiverComponent::IsTimeout(double time_stamp) {
  if (enable_timeout_ == false) {
    return false;
  }
  double cur_time = apollo::common::time::Clock::NowInSeconds();
  if (cur_time < time_stamp) {
    return true;
  }
  if (FLAGS_timeout < cur_time - time_stamp) {
    return true;
  }
  return false;
}

bool UDPBridgeMultiReceiverComponent::MsgHandle(int fd) {
  struct sockaddr_in client_addr;
  socklen_t sock_len = static_cast<socklen_t>(sizeof(client_addr));
  int bytes = 0;
  int total_recv = 2 * FRAME_SIZE;
  char total_buf[2 * FRAME_SIZE] = {0};
  bytes =
      static_cast<int>(recvfrom(fd, total_buf, total_recv, 0,
                                (struct sockaddr *)&client_addr, &sock_len));
  ADEBUG << "total recv " << bytes;
  if (bytes <= 0 || bytes > total_recv) {
    return false;
  }
  char header_flag[sizeof(BRIDGE_HEADER_FLAG) + 1] = {0};
  size_t offset = 0;
  memcpy(header_flag, total_buf, HEADER_FLAG_SIZE);
  if (strcmp(header_flag, BRIDGE_HEADER_FLAG) != 0) {
    AINFO << "header flag not match!";
    return false;
  }
  offset += sizeof(BRIDGE_HEADER_FLAG) + 1;

  char header_size_buf[sizeof(hsize) + 1] = {0};
  const char *cursor = total_buf + offset;
  memcpy(header_size_buf, cursor, sizeof(hsize));
  hsize header_size = *(reinterpret_cast<hsize *>(header_size_buf));
  if (header_size > FRAME_SIZE) {
    AINFO << "header size is more than FRAME_SIZE!";
    return false;
  }
  offset += sizeof(hsize) + 1;

  BridgeHeader header;
  size_t buf_size = header_size - offset;
  cursor = total_buf + offset;
  if (!header.Diserialize(cursor, buf_size)) {
    AINFO << "header diserialize failed!";
    return false;
  }

  ADEBUG << "proto name : " << header.GetMsgName().c_str();
  ADEBUG << "proto sequence num: " << header.GetMsgID();
  ADEBUG << "proto total frames: " << header.GetTotalFrames();
  ADEBUG << "proto frame index: " << header.GetIndex();

  std::lock_guard<std::mutex> lock(mutex_);
  ProtoDiserializedBufBase *proto_buf = CreateBridgeProtoBuf(header);
  if (!proto_buf) {
    return false;
  }

  cursor = total_buf + header_size;
  char *buf = proto_buf->GetBuf(header.GetFramePos());
  memcpy(buf, cursor, header.GetFrameSize());
  proto_buf->UpdateStatus(header.GetIndex());
  if (proto_buf->IsReadyDiserialize()) {
    proto_buf->DiserializedAndPub();
    RemoveInvalidBuf(proto_buf->GetMsgID(), proto_buf->GetMsgName());
    RemoveItem(&proto_list_, proto_buf);
  }
  return true;
}

bool UDPBridgeMultiReceiverComponent::RemoveInvalidBuf(uint32_t msg_id,
  const std::string &msg_name) {
  if (msg_id == 0) {
    return false;
  }
  std::vector<ProtoDiserializedBufBase *>::iterator itor =
      proto_list_.begin();
  for (; itor != proto_list_.end();) {
    if ((*itor)->GetMsgID() < msg_id) {
      ProtoDiserializedBufBase *tmp = *itor;
      FREE_POINTER(tmp);
      itor = proto_list_.erase(itor);
      continue;
    }
    ++itor;
  }
  return true;
}

}  // namespace bridge
}  // namespace apollo
