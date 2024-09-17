#include "../../include/datanode.h"
#include "../../include/json/json.h"
#include "../jsoncpp.cpp"
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>


Datanode::Datanode(std::string ip, int port)
    : ip_(ip), port_(port),
      acceptor_(io_context_,
                asio::ip::tcp::endpoint(
                    asio::ip::address::from_string(ip.c_str()), port)) {
  // port是datanode的地址,port + 1000是redis的地址
  std::string url = "tcp://" + ip_ + ":" + std::to_string(port_ + 1000);
  redis_ = std::make_unique<sw::redis::Redis>(url);
  filename="/home/chenximeng/cacheproject/happylrc/jsonout/"+ ip_ + std::to_string(port_) +"data.json";
  //file_.open(filename, std::ios::app | std::ios::in | std::ios::binary);
  file_.open(filename, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
  cur_offset_ = 0;
}

Datanode::~Datanode() { acceptor_.close(); }

void Datanode::keep_working() {
  for (;;) {
    asio::ip::tcp::socket peer(io_context_);
    acceptor_.accept(peer);
    /*asio flag*/
    std::vector<unsigned char> flag_buf(sizeof(int));
    asio::read(peer, asio::buffer(flag_buf, flag_buf.size()));
    int flag = bytes_to_int(flag_buf);

    if (flag == 0) {/*写redis*/
      std::vector<unsigned char> value_or_key_size_buf(sizeof(int));
      /*asio key_size*/
      asio::read(peer, asio::buffer(value_or_key_size_buf,
                                    value_or_key_size_buf.size()));
      int key_size = bytes_to_int(value_or_key_size_buf);
      /*asio value_size*/
      asio::read(peer, asio::buffer(value_or_key_size_buf,
                                    value_or_key_size_buf.size()));
      int value_size = bytes_to_int(value_or_key_size_buf);
      /*asio key-value*/
      std::string key_buf(key_size, 0);
      std::string value_buf(value_size, 0);
      asio::read(peer, asio::buffer(key_buf.data(), key_buf.size()));
      asio::read(peer, asio::buffer(value_buf.data(), value_buf.size()));

      redis_->set(key_buf, value_buf);

      std::vector<char> finish(1);
      asio::write(peer, asio::buffer(finish, finish.size()));

      asio::error_code ignore_ec;
      peer.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
      peer.close(ignore_ec);
    } else if (flag ==1) {/*读redis*/
      std::vector<unsigned char> key_size_buf(sizeof(int));
      asio::read(peer, asio::buffer(key_size_buf, key_size_buf.size()));
      int key_size = bytes_to_int(key_size_buf);

      std::string key_buf(key_size, 0);
      asio::read(peer, asio::buffer(key_buf.data(), key_buf.size()));

      auto value_returned = redis_->get(key_buf);
      my_assert(value_returned.has_value());
      std::string value = value_returned.value();

      asio::write(peer, asio::buffer(value.data(), value.length()));

      asio::error_code ignore_ec;
      peer.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
      peer.close(ignore_ec);
    } else if (flag ==2) {/*写disk*/
      std::vector<unsigned char> value_or_key_size_buf(sizeof(int));
      /*asio key_size*/
      asio::read(peer, asio::buffer(value_or_key_size_buf,
                                    value_or_key_size_buf.size()));
      int key_size = bytes_to_int(value_or_key_size_buf);
      /*asio value_size*/
      asio::read(peer, asio::buffer(value_or_key_size_buf,
                                    value_or_key_size_buf.size()));
      int value_size = bytes_to_int(value_or_key_size_buf);
      /*asio key-value*/
      std::string key_buf(key_size, 0);
      std::string value_buf(value_size, 0);
      asio::read(peer, asio::buffer(key_buf.data(), key_buf.size()));
      asio::read(peer, asio::buffer(value_buf.data(), value_buf.size()));

      //使用文件？？？？？？？？？？？？？？？？？？？？？？？？？？
      /************************************************************************/
      /*redis_->set(key_buf, value_buf);*/
      write_key_value_to_disk(key_buf, value_buf);
      

      /*************************************************************************/
      std::vector<char> finish(1);
      asio::write(peer, asio::buffer(finish, finish.size()));

      asio::error_code ignore_ec;
      peer.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
      peer.close(ignore_ec);
    } else if (flag ==3) {/*读disk*/
      std::vector<unsigned char> key_size_buf(sizeof(int));
      asio::read(peer, asio::buffer(key_size_buf, key_size_buf.size()));
      int key_size = bytes_to_int(key_size_buf);

      std::string key_buf(key_size, 0);
      asio::read(peer, asio::buffer(key_buf.data(), key_buf.size()));

      //使用文件？？？？？？？？？？？？？？？？？？？？？？？？？？
      /************************************************************************/
      /*auto value_returned = redis_->get(key_buf);
      my_assert(value_returned.has_value());
      std::string value = value_returned.value();*/
      std::string value = find_value_by_key_from_disk(key_buf);
      
      
      /*************************************************************************/
      asio::write(peer, asio::buffer(value.data(), value.length()));

      asio::error_code ignore_ec;
      peer.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
      peer.close(ignore_ec);
    } 

  }
}


void Datanode::write_key_value_to_disk(const std::string& key, const std::string& value){
  try {
    file_.seekg(0, std::ios::end);
    file_.write(value.data(), value.size());
    block_info_[key] = {cur_offset_, value.size()};
    cur_offset_ += value.size();
  } catch(const std::exception& e) {
      std::cerr << e.what() << '\n';
  }

  /*
  // 构造JSON对象
  // 使用互斥量锁定文件写入操作 应该不需要？一个块写一个node 不冲突
  std::lock_guard<std::mutex> lock(file_write_mutex);
  Json::Value root;
  root[key] = value;

  // 尝试以追加写入模式打开文件
  Json::StreamWriterBuilder builder;
  builder["indentation"] = ""; // 禁用缩进，以确保每次写入一行
  std::string json_str = Json::writeString(builder, root) + "\n"; // 添加换行符
  try {
    std::ofstream outfile(filename, std::ios::app);
    if (outfile.is_open()) {
      outfile << json_str;
      outfile.close();
      return true; // 文件写入成功
    } else {
      std::cerr << "Error: Unable to open file " << filename << " for writing." << std::endl;
      return false; // 文件打开失败
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return false; // 文件写入过程中出现异常
  }
  */
}

std::string Datanode::find_value_by_key_from_disk(const std::string& key) {
  try {
    file_.seekg(block_info_[key].first);
    std::string value;
    value.resize(block_info_[key].second);
    file_.read(value.data(), value.size());
    return value;
  } catch(const std::exception& e) {
    std::cerr << e.what() << '\n';
    return "";
  }
  /*
  std::lock_guard<std::mutex> lock(file_read_mutex);
  std::ifstream infile(filename);
  if (!infile.is_open()) {
    std::cerr << "Error: Unable to open file " << filename << " for reading." << std::endl;
    return ""; // 文件打开失败，返回空字符串
  }

  std::string line;
  while (std::getline(infile, line)) {
    Json::Value root;
    Json::Reader reader;
    if (reader.parse(line, root)) {
      if (root.isMember(key)) {
        infile.close();
        return root[key].asString();
      }
    } else {
      std::cerr << "Error: Failed to parse JSON from line: " << line << std::endl;
    }
  }

  infile.close();
  std::cerr << "Error: Key '" << key << "' not found." << std::endl;
  return ""; // 指定的键不存在，返回空字符串
  */
}



