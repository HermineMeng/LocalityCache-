#include "../../include/client.h"
#include "../../include/utils.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <fstream>
#include <chrono>
#include <string>
#include <vector>
#include <unistd.h>
using namespace std;
using namespace chrono;

int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; i++) {
    args.push_back(argv[i]);
  }

  my_assert(args.size() >= 8);

  EC_schema ec_schema;

  // partial_decoding始终为true
  // 内部暂未实现“非partial_decoding”的修复流程
  ec_schema.partial_decoding = true;

  if (args[1] == "Azure_LRC") {
    ec_schema.encode_type = Encode_Type::Azure_LRC;
  } else {
    my_assert(false);
  }

  Placement_Type placement_type;
  if (args[2] == "random") {
    ec_schema.placement_type = Placement_Type::random;
  } else {
    my_assert(false);
  }

  int value_length = -1;
  ec_schema.k = std::stoi(args[3]);
  ec_schema.real_l = std::stoi(args[4]);
  // 只考虑k被l整除的情况
  my_assert(ec_schema.real_l == -1 || ec_schema.k % ec_schema.real_l == 0);
  ec_schema.b = ec_schema.k / ec_schema.real_l;
  ec_schema.g = std::stoi(args[5]);
  ec_schema.stripe_size_upper = std::stoi(args[6]);
  value_length = std::stoi(args[7]);

  // 如果部署在集群上, 一定要将client ip地址设置为1个实际的IP地址, 而非"0.0.0.0"
  // 因为这个IP地址会被proxy使用
  Client client("100.0.0.4", CLIENT_TRANSFER_DATA_PORT, "100.0.0.4",
                COORDINATOR_RPC_PORT);

  client.set_ec_parameter(ec_schema);

  std::unordered_map<std::string, std::string> key_value;

  int num_of_kv_pairs = 100;
  generate_unique_random_strings(5, value_length, num_of_kv_pairs, key_value);

  for (auto &kv : key_value) {
    std::cout << "set kv: " << kv.first << std::endl;
    client.set(kv.first, kv.second);
  }
  
  /*
  // datanode的数量, 每个节点都修1次
  int num_of_times = 10;
  unsigned int num_of_nodes = 51;
  auto start = system_clock::now();
  for(int times = 0; times < num_of_times; times++){
    std::cout << "round ********** " << times << std::endl;
    for (unsigned int i = 0; i < num_of_nodes; i++) {
    
      std::cout << "repair node " << i << std::endl;
      client.repair({i});
    }
  }
  auto end = system_clock::now();
  
  auto duration = duration_cast<microseconds>(end - start);
  double time_cost = double(duration.count()) * microseconds::period::num / microseconds::period::den;
  std::cout << time_cost << "秒" << std::endl;
  string test_result_file = "/home/chenximeng/cacheproject/test5.result";
  ofstream fout(test_result_file, std::ios::app);
  fout <<  "时间开销：" << time_cost / num_of_times << "秒" << std::endl;
  double block_size = (double)(value_length ) / (double) ec_schema.k;
  std::cout << block_size << "B" << std::endl;
  double repair_speed = (double)num_of_kv_pairs * (double)(ec_schema.k + ec_schema.real_l + ec_schema.g) * (double)((double)block_size / 1024 /1024) / time_cost;
  fout <<  "修复速度：" << repair_speed * num_of_times << "MB/秒" << endl;
  */

  
  sleep(5);
  int num_of_times = 10;
  auto start = system_clock::now();
  for(int times = 0; times < num_of_times; times++){
    std::cout << "round ********** " << times << std::endl;
    for (auto &kv : key_value) {
      std::cout << "get kv: " << kv.first << std::endl;
      auto stored_value = client.get(kv.first);
      my_assert(stored_value == kv.second);
    }
  }
  
  auto end = system_clock::now();
  
  
  auto duration = duration_cast<microseconds>(end - start);
  double time_cost = double(duration.count()) * microseconds::period::num / microseconds::period::den;
  std::cout << time_cost << "秒" << std::endl;
  string test_result_file = "/home/chenximeng/cacheproject/testbu1.result";
  ofstream fout(test_result_file, std::ios::app);
  fout <<  "时间开销：" << time_cost / num_of_times << "秒" << std::endl;
  std::cout << time_cost / num_of_times << "秒" << std::endl;
  //fout <<  "单对象时间：" << time_cost / num_of_kv_pairs << "秒" << endl;
  //std::cout << time_cost / num_of_kv_pairs << "秒" << std::endl;
  
  

  return 0;
}