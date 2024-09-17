#include "../../include/coordinator.h"
#include "../../include/tinyxml2.h"

Coordinator::Coordinator(std::string ip, int port, std::string config_file_path)
    : ip_(ip), port_(port), config_file_path_(config_file_path), alpha_(0.5) {
  rpc_server_ = std::make_unique<coro_rpc::coro_rpc_server>(4, port_);

  rpc_server_->register_handler<&Coordinator::set_erasure_coding_parameters>(
      this);
  rpc_server_->register_handler<&Coordinator::get_proxy_location>(this);
  rpc_server_->register_handler<&Coordinator::commit_object>(this);
  rpc_server_->register_handler<&Coordinator::ask_for_data>(this);
  rpc_server_->register_handler<&Coordinator::echo>(this);
  rpc_server_->register_handler<&Coordinator::ask_for_repair>(this);

  init_cluster_info();
  init_proxy_info();
}

Coordinator::~Coordinator() { rpc_server_->stop(); }

std::string Coordinator::echo(std::string s) { return s + "zhaohao"; }

void Coordinator::start() { auto err = rpc_server_->start(); }

void Coordinator::init_cluster_info() {
  /*std::vector<double> storages = {16, 16, 16, 16, 16, 16, 16, 16, 64, 64,

                                  16, 16, 16, 16, 16, 16, 16, 16, 64, 64,

                                  16, 16, 16, 16, 16, 16, 16, 16, 64, 64,

                                  16, 16, 16, 16, 16, 16, 16, 16, 64, 64};
  std::vector<double> cache_storages = {16, 16, 16, 16, 16, 16, 16, 16, 64, 64,

                                  16, 16, 16, 16, 16, 16, 16, 16, 64, 64,

                                  16, 16, 16, 16, 16, 16, 16, 16, 64, 64,

                                  16, 16, 16, 16, 16, 16, 16, 16, 64, 64};*/


  tinyxml2::XMLDocument xml;
  xml.LoadFile(config_file_path_.c_str());
  tinyxml2::XMLElement *root = xml.RootElement();
  unsigned int node_id = 0;
  /*cache_node_id编号*/
  unsigned int cache_node_id = 0;
  

  for (tinyxml2::XMLElement *cluster = root->FirstChildElement();
       cluster != nullptr; cluster = cluster->NextSiblingElement()) {
    /*指向AZ*/
    unsigned int cluster_id(std::stoi(cluster->Attribute("id")));
    std::string proxy(cluster->Attribute("proxy"));

    cluster_info_[cluster_id].cluster_id = cluster_id;
    auto pos = proxy.find(':');
    cluster_info_[cluster_id].proxy_ip = proxy.substr(0, pos);
    cluster_info_[cluster_id].proxy_port =
        std::stoi(proxy.substr(pos + 1, proxy.size()));

    for (tinyxml2::XMLElement *node =
             cluster->FirstChildElement()->FirstChildElement();
         node != nullptr; node = node->NextSiblingElement()) {
      /*指向第一个 <datanode> 元素*/
      cluster_info_[cluster_id].nodes.push_back(node_id);

      std::string node_uri(node->Attribute("uri"));
      node_info_[node_id].node_id = node_id;
      auto pos = node_uri.find(':');
      node_info_[node_id].ip = node_uri.substr(0, pos);
      node_info_[node_id].port =
          std::stoi(node_uri.substr(pos + 1, node_uri.size()));
      node_info_[node_id].cluster_id = cluster_id;

      /*node_info_[node_id].storage = storages[node_id];
      node_info_[node_id].storage_cost = 0;
      node_info_[node_id].network_cost = 0;*/

      node_id++;
    }

    /*录入cachenode信息*/
    for (tinyxml2::XMLElement *cache_node =
             cluster->FirstChildElement()->NextSiblingElement()->FirstChildElement();
         cache_node != nullptr; cache_node = cache_node->NextSiblingElement()) {
      /*指向第一个 <datanode> 元素*/
      cluster_info_[cluster_id].cache_nodes.push_back(cache_node_id);

      std::string cache_node_uri(cache_node->Attribute("uri"));
      cache_node_info_[cache_node_id].cache_node_id = cache_node_id;
      auto pos = cache_node_uri.find(':');
      cache_node_info_[cache_node_id].ip = cache_node_uri.substr(0, pos);
      cache_node_info_[cache_node_id].port =
          std::stoi(cache_node_uri.substr(pos + 1, cache_node_uri.size()));
      cache_node_info_[cache_node_id].cluster_id = cluster_id;

      /*cache_node_info_[cache_node_id].storage = cache_storages[cache_node_id];
      cache_node_info_[cache_node_id].storage_cost = 0;
      cache_node_info_[cache_node_id].network_cost = 0;*/

      cache_node_id++;
    }
  }

}

void Coordinator::init_proxy_info() {
  for (auto cur = cluster_info_.begin(); cur != cluster_info_.end(); cur++) {
    connect_to_proxy(cur->second.proxy_ip, cur->second.proxy_port);
  }
}

void Coordinator::set_erasure_coding_parameters(EC_schema ec_schema) {
  ec_schema_ = ec_schema;
}

std::pair<std::string, int> Coordinator::get_proxy_location(std::string key,
                                                            size_t value_len) {
  mutex_.lock();
  if (commited_object_info_.contains(key)) {
    mutex_.unlock();
    my_assert(false);
  }
  mutex_.unlock();

  meta_info_of_object new_object;
  new_object.value_len = value_len;

  placement_info placement;

  if (ec_schema_.stripe_size_upper >= new_object.value_len) {
    size_t block_size = std::ceil(static_cast<double>(new_object.value_len) /
                                  static_cast<double>(ec_schema_.k));
    block_size = 64 * std::ceil(static_cast<double>(block_size) / 64.0);

    auto &stripe = new_stripe_item(block_size);
    new_object.stripes.push_back(stripe.stripe_id);

    generate_placement_plan(stripe.nodes, stripe.cache_nodes, stripe.stripe_id);
    std::cout<<"generate_placement_plan success"<<std::endl;

    init_placement(placement, key, value_len, block_size, -1);

    placement.stripe_ids.push_back(stripe.stripe_id);
    for (auto &node_id : stripe.nodes) {
      auto &node = node_info_[node_id];
      placement.datanode_ip_port.push_back({node.ip, node.port});
    }
    /**/
    for (auto &cache_node_id : stripe.cache_nodes) {
      auto &cache_node = cache_node_info_[cache_node_id];
      placement.cachenode_ip_port.push_back({cache_node.ip, cache_node.port});
    }
  } else {
    size_t block_size =
        std::ceil(static_cast<double>(ec_schema_.stripe_size_upper) /
                  static_cast<double>(ec_schema_.k));
    block_size = 64 * std::ceil(static_cast<double>(block_size) / 64.0);

    init_placement(placement, key, value_len, block_size, -1);

    int num_of_stripes = value_len / (ec_schema_.k * block_size);
    size_t left_value_len = value_len;
    for (int i = 0; i < num_of_stripes; i++) {
      left_value_len -= ec_schema_.k * block_size;

      auto &stripe = new_stripe_item(block_size);
      new_object.stripes.push_back(stripe.stripe_id);

      generate_placement_plan(stripe.nodes, stripe.cache_nodes, stripe.stripe_id);
      std::cout<< "generate_placement_plan success" <<std::endl;

      placement.stripe_ids.push_back(stripe.stripe_id);
      for (auto &node_id : stripe.nodes) {
        auto &node = node_info_[node_id];
        placement.datanode_ip_port.push_back({node.ip, node.port});
      }
      /**/
      for (auto &cache_node_id : stripe.cache_nodes) {
        auto &cache_node = cache_node_info_[cache_node_id];
        placement.cachenode_ip_port.push_back({cache_node.ip, cache_node.port});
      }
    }
    if (left_value_len > 0) {
      size_t tail_block_size = std::ceil(static_cast<double>(left_value_len) /
                                         static_cast<double>(ec_schema_.k));
      tail_block_size =
          64 * std::ceil(static_cast<double>(tail_block_size) / 64.0);

      placement.tail_block_size = tail_block_size;
      auto &stripe = new_stripe_item(tail_block_size);
      new_object.stripes.push_back(stripe.stripe_id);

      generate_placement_plan(stripe.nodes, stripe.cache_nodes, stripe.stripe_id);

      placement.stripe_ids.push_back(stripe.stripe_id);
      for (auto &node_id : stripe.nodes) {
        auto &node = node_info_[node_id];
        placement.datanode_ip_port.push_back({node.ip, node.port});
      }
      /**/
      for (auto &cache_node_id : stripe.cache_nodes) {
        auto &cache_node = cache_node_info_[cache_node_id];
        placement.cachenode_ip_port.push_back({cache_node.ip, cache_node.port});
      }
    } else {
      placement.tail_block_size = -1;
    }
  }

  mutex_.lock();
  objects_waiting_commit_[key] = new_object;
  mutex_.unlock();

  std::cout<<"before connect proxy"<<std::endl;



  std::pair<std::string, int> proxy_location;
  //避开最后一个cluster
  unsigned int selected_cluster_id = random_index(cluster_info_.size()-1);
  std::string selected_proxy_ip = cluster_info_[selected_cluster_id].proxy_ip;
  int selected_proxy_port = cluster_info_[selected_cluster_id].proxy_port;
  proxy_location = {selected_proxy_ip, selected_proxy_port};

  std::cout<<proxy_location.first<<proxy_location.second<<std::endl;

  async_simple::coro::syncAwait(
      proxys_[selected_proxy_ip + std::to_string(selected_proxy_port)]
          ->call<&Proxy::start_encode_and_store_object>(placement));

  return proxy_location;
}


static bool cmp_combined_cost(std::pair<unsigned int, double> &a,
                              std::pair<unsigned int, double> &b) {
  return a.second < b.second;
}

void Coordinator::generate_placement_plan(std::vector<unsigned int> &nodes,
                                          std::vector<unsigned int> &cache_nodes,
                                          unsigned int stripe_id) {
  // 我的理解,flat放置不用单独实现,在实验室限制服务器网速即可
  // 因此这里没有实现flag放置
  /*generate_placement_plan(stripe.nodes, stripe.cache_nodes, stripe.stripe_id);*/
  stripe_item &stripe = stripe_info_[stripe_id];
  int k = stripe.k;
  int real_l = stripe.real_l;
  int b = stripe.b;
  int g = stripe.g;
  my_assert(k % real_l == 0);

  if (stripe.placement_type == Placement_Type::random) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned int> dis(0, node_info_.size() - 1);

    std::vector<bool> visited_nodes(node_info_.size(), false);
    /**/
    std::vector<bool> visited_cache_nodes(cache_node_info_.size(), false);
    // (某个cluster中已经包含的group编号, 某个cluster中已经存放的块数量)[cache是否需要？？？]
    std::vector<std::pair<std::unordered_set<int>, int>> help(
        cluster_info_.size());
    for (auto i = 0; i < cluster_info_.size(); i++) {
      help[i].second = 0;
    }
    unsigned int node_id;
    /**/
    unsigned int cache_node_id;
    unsigned int cluster_id;
    int space_upper;
    auto find_a_node_for_a_block = [&, this]() {
      do {
        node_id = dis(gen);
        cluster_id = node_info_[node_id].cluster_id;
        space_upper = g + help[cluster_id].first.size();
      } while (visited_nodes[node_id] == true ||
               help[cluster_id].second == space_upper || node_id == 50);

      my_assert(help[cluster_id].second < space_upper);
      my_assert(visited_nodes[node_id] == false);

      nodes.push_back(node_id);
      node_info_[node_id].stripe_ids.insert(stripe_id);
      visited_nodes[node_id] = true;
      help[cluster_id].second++;

      return cluster_id;
    };  
  
    auto find_specific_node_for_a_block = [&, this]() {
      node_id = 50;
      cluster_id = node_info_[node_id].cluster_id;
      my_assert(cluster_id == 5);
      nodes.push_back(node_id);
      node_info_[node_id].stripe_ids.insert(stripe_id);
      /*
      do {
        node_id = dis(gen);
        cluster_id = node_info_[node_id].cluster_id;
        space_upper = g + help[cluster_id].first.size();
      } while (visited_nodes[node_id] == true ||
               help[cluster_id].second == space_upper);

      my_assert(help[cluster_id].second < space_upper);
      my_assert(visited_nodes[node_id] == false);

      nodes.push_back(node_id);
      node_info_[node_id].stripe_ids.insert(stripe_id);
      visited_nodes[node_id] = true;
      help[cluster_id].second++;
      */

      return cluster_id;
    };  

    /*写入cache_node，在指定的cluster_id中随机cache_node*/
    auto find_a_cache_node_for_a_block = [&, this](unsigned int cluster_id) {
      cluster_item &cluster = cluster_info_[cluster_id];
      /*cache_node随机*/
      std::random_device rd_cache_node;
      std::mt19937 gen_node(rd_cache_node());
      std::uniform_int_distribution<unsigned int> dis_cache_node(
          0, cluster.cache_nodes.size() - 1);
      int cache_node_idx;
      /*
      do {
        cache_node_idx = dis_cache_node(gen_node);       
        //cache_node没有单簇容错的要求
        //space_upper = g + help[cluster_id].first.size();
      } while (visited_cache_nodes[cluster.cache_nodes[cache_node_idx]] == true );
      */
      cache_node_idx = dis_cache_node(gen_node); 
      //visited_cache_nodes[cluster.cache_nodes[cache_node_idx]] = true;
      /*cache_node_id=cluster.cache_nodes[cache_node_idx];*/
      cache_nodes.push_back(cluster.cache_nodes[cache_node_idx]);
      cache_node_info_[cluster.cache_nodes[cache_node_idx]].stripe_ids.insert(stripe_id);
      
    };

    // 数据块
    for (int i = 0; i < real_l; i++) {
      for (int j = 0; j < b; j++) {
        if( (i == 1 && j == 1) || (i == 1 && j == 0)){
          /*|| (i == 1 && j == 0)*/
          unsigned int cluster_id = find_specific_node_for_a_block();
          help[cluster_id].first.insert(i);
        }else{
          unsigned int cluster_id = find_a_node_for_a_block();
          help[cluster_id].first.insert(i);
        }
        /*
        unsigned int cluster_id = find_a_node_for_a_block();
        help[cluster_id].first.insert(i);
        */
      }
    }


    // 全局校验块
    for (int i = 0; i < g; i++) {
      unsigned int cluster_id = find_a_node_for_a_block();
      /*任选一个× 选择最后一个√ global parity插入本cluster中的cache_node*/
      
      if(i== (g-1)){
        find_a_cache_node_for_a_block(cluster_id);
      }
      
    }

    // 局部校验块
    for (int i = 0; i < real_l; i++) {
      unsigned int cluster_id = find_a_node_for_a_block();
      help[cluster_id].first.insert(i);
      /*插入本cluster中的cache_node*/
      find_a_cache_node_for_a_block(cluster_id);
    }
  } else {
    my_assert(false);
  }
  /*else if (stripe.placement_type == Placement_Type::strategy_ECWIDE) {
    // partition plan的每个元素：{num of data, num of local, num of global}
    std::vector<std::vector<int>> partition_plan =
        partition_strategy_ECWIDE(k, g, b);

    // ECWIDE没有考虑load balance,随机选择节点存放
    select_by_random(partition_plan, nodes, stripe_id);
  }*/
  
}





stripe_item &Coordinator::new_stripe_item(size_t block_size) {
  stripe_item temp;
  temp.stripe_id = next_stripe_id_++;
  stripe_info_[temp.stripe_id] = temp;
  stripe_item &stripe = stripe_info_[temp.stripe_id];
  stripe.encode_type = ec_schema_.encode_type;
  stripe.placement_type = ec_schema_.placement_type;
  stripe.k = ec_schema_.k;
  stripe.real_l = ec_schema_.real_l;
  stripe.g = ec_schema_.g;
  stripe.b = ec_schema_.b;
  stripe.block_size = block_size;

  return stripe_info_[temp.stripe_id];
}

void Coordinator::init_placement(placement_info &placement, std::string key,
                                 size_t value_len, size_t block_size,
                                 size_t tail_block_size) {
  placement.encode_type = ec_schema_.encode_type;
  placement.key = key;
  placement.value_len = value_len;
  placement.k = ec_schema_.k;
  placement.g = ec_schema_.g;
  placement.real_l = ec_schema_.real_l;
  placement.b = ec_schema_.b;
  placement.block_size = block_size;
  placement.tail_block_size = tail_block_size;
}

void Coordinator::connect_to_proxy(std::string ip, int port) {
  std::string location = ip + std::to_string(port);
  my_assert(proxys_.contains(location) == false);

  proxys_[location] = std::make_unique<coro_rpc::coro_rpc_client>();
  async_simple::coro::syncAwait(
      proxys_[location]->connect(ip, std::to_string(port + 1000)));
}

/*client写数据后call*/
void Coordinator::commit_object(std::string key) {
  std::unique_lock<std::mutex> lck(mutex_);
  my_assert(commited_object_info_.contains(key) == false &&
            objects_waiting_commit_.contains(key) == true);
  commited_object_info_[key] = objects_waiting_commit_[key];
  objects_waiting_commit_.erase(key);
}

size_t Coordinator::ask_for_data(std::string key, std::string client_ip,
                                 int client_port) {
  mutex_.lock();
  if (commited_object_info_.contains(key) == false) {
    mutex_.unlock();
    my_assert(false);
  }
  meta_info_of_object &object = commited_object_info_[key];
  mutex_.unlock();
  /*获取placement所有信息 传给proxy*/
  placement_info placement;
  if (ec_schema_.stripe_size_upper >= object.value_len) {
    size_t block_size = std::ceil(static_cast<double>(object.value_len) /
                                  static_cast<double>(ec_schema_.k));
    block_size = 64 * std::ceil(static_cast<double>(block_size) / 64.0);
    init_placement(placement, key, object.value_len, block_size, -1);
  } else {
    size_t block_size =
        std::ceil(static_cast<double>(ec_schema_.stripe_size_upper) /
                  static_cast<double>(ec_schema_.k));
    block_size = 64 * std::ceil(static_cast<double>(block_size) / 64.0);

    size_t tail_block_size = -1;
    if (object.value_len % (ec_schema_.k * block_size) != 0) {
      size_t tail_stripe_size = object.value_len % (ec_schema_.k * block_size);
      tail_block_size = std::ceil(static_cast<double>(tail_stripe_size) /
                                  static_cast<double>(ec_schema_.k));
      tail_block_size =
          64 * std::ceil(static_cast<double>(tail_block_size) / 64.0);
    }
    init_placement(placement, key, object.value_len, block_size,
                   tail_block_size);
  }

  for (auto stripe_id : object.stripes) {
    stripe_item &stripe = stripe_info_[stripe_id];
    placement.stripe_ids.push_back(stripe_id);

    /*
    // 因为只读取数据块, 所以只会对存储了数据块的节点网络开销+1
    for (auto block_idx = 0; block_idx <= placement.k - 1; block_idx++) {
      unsigned int node_id = stripe.nodes[block_idx];
      node_info_[node_id].network_cost += 1;
    }*/

    for (auto node_id : stripe.nodes) {
      node_item &node = node_info_[node_id];
      placement.datanode_ip_port.push_back({node.ip, node.port});
    }
    /****************/
    for (auto cache_node_id : stripe.cache_nodes) {
      cache_node_item &cache_node = cache_node_info_[cache_node_id];
      placement.cachenode_ip_port.push_back({cache_node.ip, cache_node.port});
    }
  }

  placement.client_ip = client_ip;
  placement.client_port = client_port;
  
  //避开最后一个受限的集群
  int selected_proxy_id = random_index(cluster_info_.size()-1);
  std::string location =
      cluster_info_[selected_proxy_id].proxy_ip +
      std::to_string(cluster_info_[selected_proxy_id].proxy_port);
  /*****************************************************************/
  std::cout << location<< std::endl;
  async_simple::coro::syncAwait(
      proxys_[location]->call<&Proxy::decode_and_transfer_data_baseline>(placement));
  /*返回的是value_len，用于buf_size*/
  return object.value_len;
}


void Coordinator::ask_for_repair(std::vector<unsigned int> failed_node_ids) {
  // 目前仅实现了single node repair
  my_assert(failed_node_ids.size() == 1);

  // 找到所有数据损坏的条带
  std::unordered_set<unsigned int> failed_stripe_ids;
  for (auto node_id : failed_node_ids) {
    for (auto stripe_id : node_info_[node_id].stripe_ids) {
      failed_stripe_ids.insert(stripe_id);
    }
  }

  for (auto stripe_id : failed_stripe_ids) {
    // 找到条带内的哪一个block损坏了
    std::vector<int> failed_block_indexes;
    for (auto i = 0; i < stripe_info_[stripe_id].nodes.size(); i++) {
      if (stripe_info_[stripe_id].nodes[i] == failed_node_ids[0]) {
        failed_block_indexes.push_back(i);
      }
    }
    my_assert(failed_block_indexes.size() == 1);

    if (stripe_info_[stripe_id].encode_type == Encode_Type::Azure_LRC) {
      do_repair(stripe_id, {failed_block_indexes[0]});
    }
  }
}

////////////////////////////////////////////////////
///////////////new do_repair
////////////////////////////////////////////////////
//////////////////////do_repair
void Coordinator::do_repair_CACHED(unsigned int stripe_id,
                            std::vector<int> failed_block_indexes) {
  my_assert(failed_block_indexes.size() == 1);

  // 记录了本次修复涉及的cluster id
  std::vector<unsigned int> repair_span_cluster;
  // 记录了需要从哪些cluster中,读取哪些block,记录顺序和repair_span_cluster对应
  // cluster_id->vector((node_ip, node_port), block_index)
  std::vector<std::vector<std::pair<std::pair<std::string, int>, int>>>
      blocks_to_read_in_each_cluster;
  // 记录了修复后block应存放的位置
  // (node_id, block_index)
  std::vector<std::pair<unsigned int, int>> new_locations_with_block_index;
  generate_repair_plan_CACHED(stripe_id, failed_block_indexes,
                       blocks_to_read_in_each_cluster, repair_span_cluster,
                       new_locations_with_block_index);

  my_assert(repair_span_cluster.size() > 0);
  unsigned int main_cluster_id = repair_span_cluster[0];
  stripe_item &stripe = stripe_info_[stripe_id];
  int failed_block_index = failed_block_indexes[0];
  if(failed_block_index >= 0 && failed_block_index < stripe.k + stripe.g - 1){
    std::vector<std::thread> repairers;
    for (auto i = 0; i < repair_span_cluster.size(); i++) {
      if (i == 0) {
        // main cluster
        my_assert(repair_span_cluster[i] == main_cluster_id);
        repairers.push_back(std::thread([&, this, i, main_cluster_id] {
          stripe_item &stripe = stripe_info_[stripe_id];
          main_repair_plan repair_plan;
          repair_plan.k = stripe.k;
          repair_plan.real_l = stripe.real_l;
          repair_plan.g = stripe.g;
          repair_plan.b = stripe.b;
          repair_plan.encode_type = stripe.encode_type;
          repair_plan.partial_decoding = ec_schema_.partial_decoding;
          repair_plan.multi_clusters_involved = (repair_span_cluster.size() > 1);
          repair_plan.block_size = stripe.block_size;
          repair_plan.stripe_id = stripe.stripe_id;
          repair_plan.cluster_id = main_cluster_id;
          repair_plan.failed_blocks_index = failed_block_indexes;

          repair_plan.inner_cluster_help_blocks_info =
              blocks_to_read_in_each_cluster[0];
          for (auto j = 0; j < blocks_to_read_in_each_cluster.size(); j++) {
            for (auto t = 0; t < blocks_to_read_in_each_cluster[j].size(); t++) {
              repair_plan.live_blocks_index.push_back(
                  blocks_to_read_in_each_cluster[j][t].second);
            }
          }

          my_assert(new_locations_with_block_index.size() == 1);
          node_item &node = node_info_[new_locations_with_block_index[0].first];
          repair_plan.new_locations.push_back(
              {{node.ip, node.port}, new_locations_with_block_index[0].second});

            for (auto cluster_id : repair_span_cluster) {
            if (cluster_id != main_cluster_id) {
              repair_plan.help_cluster_ids.push_back(cluster_id);
            }
          }

          std::string proxy_ip = cluster_info_[main_cluster_id].proxy_ip;
          int port = cluster_info_[main_cluster_id].proxy_port;
          async_simple::coro::syncAwait(
              proxys_[proxy_ip + std::to_string(port)]->call<&Proxy::main_repair>(
                  repair_plan));
        }));
      } else {
        // help cluster
        unsigned self_cluster_id = repair_span_cluster[i];
        repairers.push_back(std::thread([&, this, i, self_cluster_id]() {
          stripe_item &stripe = stripe_info_[stripe_id];
          help_repair_plan repair_plan;
          repair_plan.k = stripe.k;
          repair_plan.real_l = stripe.real_l;
          repair_plan.g = stripe.g;
          repair_plan.b = stripe.b;
          repair_plan.encode_type = stripe.encode_type;
          repair_plan.partial_decoding = ec_schema_.partial_decoding;
          repair_plan.multi_clusters_involved = (repair_span_cluster.size() > 1);
          repair_plan.block_size = stripe.block_size;
          repair_plan.stripe_id = stripe.stripe_id;
          repair_plan.cluster_id = self_cluster_id;
          repair_plan.failed_blocks_index = failed_block_indexes;
          repair_plan.main_proxy_ip = cluster_info_[main_cluster_id].proxy_ip;
          repair_plan.main_proxy_port = cluster_info_[main_cluster_id].proxy_port;

          repair_plan.inner_cluster_help_blocks_info =
              blocks_to_read_in_each_cluster[i];
          for (auto j = 0; j < blocks_to_read_in_each_cluster.size(); j++) {
            for (auto t = 0; t < blocks_to_read_in_each_cluster[j].size(); t++) {
              repair_plan.live_blocks_index.push_back(
                  blocks_to_read_in_each_cluster[j][t].second);
            }
          }

          async_simple::coro::syncAwait(
              proxys_[cluster_info_[self_cluster_id].proxy_ip +
                      std::to_string(cluster_info_[self_cluster_id].proxy_port)]
                  ->call<&Proxy::help_repair>(repair_plan));
        }));
      }
    }

    my_assert(repair_span_cluster[0] == main_cluster_id);

    for (auto i = 0; i < repairers.size(); i++) {
      repairers[i].join();
    }
  }else{
    //修复有cache备份的全局校验块和局部校验块
    main_repair_plan repair_plan;
    repair_plan.k = stripe.k;
    repair_plan.real_l = stripe.real_l;
    repair_plan.g = stripe.g;
    repair_plan.b = stripe.b;
    repair_plan.encode_type = stripe.encode_type;
    repair_plan.partial_decoding = ec_schema_.partial_decoding;
    /*bool multi_clusters_involved;*/   
    repair_plan.single_cluster_involved = (repair_span_cluster.size() == 1);
    repair_plan.block_size = stripe.block_size;
    repair_plan.stripe_id = stripe.stripe_id;
    repair_plan.cluster_id = main_cluster_id;
    repair_plan.failed_blocks_index = failed_block_indexes;
        
    repair_plan.inner_cluster_help_blocks_info =
        blocks_to_read_in_each_cluster[0];
    /*for (auto j = 0; j < blocks_to_read_in_each_cluster.size(); j++) {
      for (auto t = 0; t < blocks_to_read_in_each_cluster[j].size(); t++) {
        repair_plan.live_blocks_index.push_back(
            blocks_to_read_in_each_cluster[j][t].second);
      }
    }*/

    my_assert(new_locations_with_block_index.size() == 1);
    node_item &node = node_info_[new_locations_with_block_index[0].first];
    repair_plan.new_locations.push_back(
        {{node.ip, node.port}, new_locations_with_block_index[0].second});

    /*for (auto cluster_id : repair_span_cluster) {
      if (cluster_id != main_cluster_id) {
        repair_plan.help_cluster_ids.push_back(cluster_id);
      }
    }*/

    std::string proxy_ip = cluster_info_[main_cluster_id].proxy_ip;
    int port = cluster_info_[main_cluster_id].proxy_port;
    async_simple::coro::syncAwait(
        proxys_[proxy_ip + std::to_string(port)]->call<&Proxy::cache_repair>(
            repair_plan));


  }
  // stripe_info_[stripe_id].nodes[new_locations_with_block_index[0].second] =
  //     new_locations_with_block_index[0].first;
}




///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////


void Coordinator::do_repair(unsigned int stripe_id,
                            std::vector<int> failed_block_indexes) {
  my_assert(failed_block_indexes.size() == 1);

  // 记录了本次修复涉及的cluster id
  std::vector<unsigned int> repair_span_cluster;
  // 记录了需要从哪些cluster中,读取哪些block,记录顺序和repair_span_cluster对应
  // cluster_id->vector((node_ip, node_port), block_index)
  std::vector<std::vector<std::pair<std::pair<std::string, int>, int>>>
      blocks_to_read_in_each_cluster;
  // 记录了修复后block应存放的位置
  // (node_id, block_index)
  std::vector<std::pair<unsigned int, int>> new_locations_with_block_index;
  generate_repair_plan(stripe_id, failed_block_indexes,
                       blocks_to_read_in_each_cluster, repair_span_cluster,
                       new_locations_with_block_index);

  my_assert(repair_span_cluster.size() > 0);
  unsigned int main_cluster_id = repair_span_cluster[0];

  std::vector<std::thread> repairers;
  for (auto i = 0; i < repair_span_cluster.size(); i++) {
    if (i == 0) {
      // main cluster
      my_assert(repair_span_cluster[i] == main_cluster_id);
      repairers.push_back(std::thread([&, this, i, main_cluster_id] {
        stripe_item &stripe = stripe_info_[stripe_id];
        main_repair_plan repair_plan;
        repair_plan.k = stripe.k;
        repair_plan.real_l = stripe.real_l;
        repair_plan.g = stripe.g;
        repair_plan.b = stripe.b;
        repair_plan.encode_type = stripe.encode_type;
        repair_plan.partial_decoding = ec_schema_.partial_decoding;
        repair_plan.multi_clusters_involved = (repair_span_cluster.size() > 1);
        repair_plan.block_size = stripe.block_size;
        repair_plan.stripe_id = stripe.stripe_id;
        repair_plan.cluster_id = main_cluster_id;
        repair_plan.failed_blocks_index = failed_block_indexes;

        repair_plan.inner_cluster_help_blocks_info =
            blocks_to_read_in_each_cluster[0];
        for (auto j = 0; j < blocks_to_read_in_each_cluster.size(); j++) {
          for (auto t = 0; t < blocks_to_read_in_each_cluster[j].size(); t++) {
            repair_plan.live_blocks_index.push_back(
                blocks_to_read_in_each_cluster[j][t].second);
          }
        }

        my_assert(new_locations_with_block_index.size() == 1);
        node_item &node = node_info_[new_locations_with_block_index[0].first];
        repair_plan.new_locations.push_back(
            {{node.ip, node.port}, new_locations_with_block_index[0].second});

        for (auto cluster_id : repair_span_cluster) {
          if (cluster_id != main_cluster_id) {
            repair_plan.help_cluster_ids.push_back(cluster_id);
          }
        }

        std::string proxy_ip = cluster_info_[main_cluster_id].proxy_ip;
        int port = cluster_info_[main_cluster_id].proxy_port;
        async_simple::coro::syncAwait(
            proxys_[proxy_ip + std::to_string(port)]->call<&Proxy::main_repair>(
                repair_plan));
      }));
    } else {
      // help cluster
      unsigned self_cluster_id = repair_span_cluster[i];
      repairers.push_back(std::thread([&, this, i, self_cluster_id]() {
        stripe_item &stripe = stripe_info_[stripe_id];
        help_repair_plan repair_plan;
        repair_plan.k = stripe.k;
        repair_plan.real_l = stripe.real_l;
        repair_plan.g = stripe.g;
        repair_plan.b = stripe.b;
        repair_plan.encode_type = stripe.encode_type;
        repair_plan.partial_decoding = ec_schema_.partial_decoding;
        repair_plan.multi_clusters_involved = (repair_span_cluster.size() > 1);
        repair_plan.block_size = stripe.block_size;
        repair_plan.stripe_id = stripe.stripe_id;
        repair_plan.cluster_id = self_cluster_id;
        repair_plan.failed_blocks_index = failed_block_indexes;
        repair_plan.main_proxy_ip = cluster_info_[main_cluster_id].proxy_ip;
        repair_plan.main_proxy_port = cluster_info_[main_cluster_id].proxy_port;

        repair_plan.inner_cluster_help_blocks_info =
            blocks_to_read_in_each_cluster[i];
        for (auto j = 0; j < blocks_to_read_in_each_cluster.size(); j++) {
          for (auto t = 0; t < blocks_to_read_in_each_cluster[j].size(); t++) {
            repair_plan.live_blocks_index.push_back(
                blocks_to_read_in_each_cluster[j][t].second);
          }
        }

        async_simple::coro::syncAwait(
            proxys_[cluster_info_[self_cluster_id].proxy_ip +
                    std::to_string(cluster_info_[self_cluster_id].proxy_port)]
                ->call<&Proxy::help_repair>(repair_plan));
      }));
    }
  }

  my_assert(repair_span_cluster[0] == main_cluster_id);

  for (auto i = 0; i < repairers.size(); i++) {
    repairers[i].join();
  }

  // stripe_info_[stripe_id].nodes[new_locations_with_block_index[0].second] =
  //     new_locations_with_block_index[0].first;
}

static bool cmp_num_live_blocks(std::pair<unsigned int, std::vector<int>> &a,
                                std::pair<unsigned int, std::vector<int>> &b) {
  return a.second.size() > b.second.size();
}




//////////////////////repair_plan新
void Coordinator::generate_repair_plan_CACHED(
    unsigned int stripe_id, std::vector<int> &failed_block_indexes,
    std::vector<std::vector<std::pair<std::pair<std::string, int>, int>>>
        &blocks_to_read_in_each_cluster,
    std::vector<unsigned int> &repair_span_cluster,
    std::vector<std::pair<unsigned int, int>> &new_locations_with_block_index) {
  stripe_item &stripe = stripe_info_[stripe_id];
  int k = stripe.k;
  int real_l = stripe.real_l;
  int g = stripe.g;
  int b = stripe.b;

  int failed_block_index = failed_block_indexes[0];
  node_item &failed_node = node_info_[stripe.nodes[failed_block_index]];
  unsigned int main_cluster_id = failed_node.cluster_id;
  repair_span_cluster.push_back(main_cluster_id);

  // 将修复好的块放回原位
  new_locations_with_block_index.push_back(
      {failed_node.node_id, failed_block_index});

  my_assert(failed_block_index >= 0 &&
            failed_block_index <= (k + g + real_l - 1));
  if (failed_block_index >= k && failed_block_index < (k + g - 1)) {
    // 修复全局校验块
    std::unordered_map<unsigned int, std::vector<int>>
        live_blocks_in_each_cluster;
    // 找到每个cluster中的存活块
    for (int i = 0; i < (k + g - 1); i++) {
      if (i != failed_block_index) {
        node_item &live_node = node_info_[stripe.nodes[i]];
        live_blocks_in_each_cluster[live_node.cluster_id].push_back(i);
      }
    }

    std::unordered_map<unsigned int, std::vector<int>>
        live_blocks_needed_in_each_cluster;
    int num_of_needed_live_blocks = k;
    // 优先读取main cluster的,即损坏块所在cluster
    for (auto live_block_index : live_blocks_in_each_cluster[main_cluster_id]) {
      if (num_of_needed_live_blocks <= 0) {
        break;
      }
      live_blocks_needed_in_each_cluster[main_cluster_id].push_back(
          live_block_index);
      num_of_needed_live_blocks--;
    }

    // 需要对剩下的cluster中存活块的数量进行排序,优先从存活块数量多的cluster中读取
    std::vector<std::pair<unsigned int, std::vector<int>>>
        sorted_live_blocks_in_each_cluster;
    for (auto &cluster : live_blocks_in_each_cluster) {
      if (cluster.first != main_cluster_id) {
        sorted_live_blocks_in_each_cluster.push_back(
            {cluster.first, cluster.second});
      }
    }
    std::sort(sorted_live_blocks_in_each_cluster.begin(),
              sorted_live_blocks_in_each_cluster.end(), cmp_num_live_blocks);
    for (auto &cluster : sorted_live_blocks_in_each_cluster) {
      for (auto &block_index : cluster.second) {
        if (num_of_needed_live_blocks <= 0) {
          break;
        }
        live_blocks_needed_in_each_cluster[cluster.first].push_back(
            block_index);
        num_of_needed_live_blocks--;
      }
    }

    // 记录需要从main cluster中读取的存活块
    std::vector<std::pair<std::pair<std::string, int>, int>>
        blocks_to_read_in_main_cluster;
    for (auto &block_index :
         live_blocks_needed_in_each_cluster[main_cluster_id]) {
      node_item &node = node_info_[stripe.nodes[block_index]];
      blocks_to_read_in_main_cluster.push_back(
          {{node.ip, node.port}, block_index});

      node.network_cost += 1;
    }
    blocks_to_read_in_each_cluster.push_back(blocks_to_read_in_main_cluster);

    // 记录需要从其它cluster中读取的存活块
    for (auto &cluster : live_blocks_needed_in_each_cluster) {
      if (cluster.first != main_cluster_id) {
        repair_span_cluster.push_back(cluster.first);

        std::vector<std::pair<std::pair<std::string, int>, int>>
            blocks_to_read_in_another_cluster;
        for (auto &block_index : cluster.second) {
          node_item &node = node_info_[stripe.nodes[block_index]];
          blocks_to_read_in_another_cluster.push_back(
              {{node.ip, node.port}, block_index});

          node.network_cost += 1;
        }
        blocks_to_read_in_each_cluster.push_back(
            blocks_to_read_in_another_cluster);
      }
    }
  } else if(failed_block_index < k){
    // 修复数据块          和局部校验块×
    int group_index = -1;
    /*if (failed_block_index >= 0 && failed_block_index <= (k - 1)) {
      group_index = failed_block_index / b;
    } else {
      group_index = failed_block_index - (k + g);
    }*/

    group_index = failed_block_index / b;

    std::vector<std::pair<unsigned int, int>> live_blocks_in_group;
    for (int i = 0; i < b; i++) {
      int block_index = group_index * b + i;
      if (block_index != failed_block_index) {
        if (block_index >= k) {
          break;
        }
        live_blocks_in_group.push_back(
            {stripe.nodes[block_index], block_index});
      }
    }
    if (failed_block_index != k + g + group_index) {
      //live_blocks_in_group.push_back(
          //{stripe.nodes[k + g + group_index], k + g + group_index});
      //从cache_node读
      live_blocks_in_group.push_back(
        {stripe.cache_nodes[group_index + 1], k + g + group_index});
    }

    std::unordered_set<unsigned int> span_cluster;
    for (auto &live_block : live_blocks_in_group) {
      //span_cluster.insert(node_info_[live_block.first].cluster_id);
      if(live_block.second > k){
        span_cluster.insert(cache_node_info_[live_block.first].cluster_id);
      }else{
        span_cluster.insert(node_info_[live_block.first].cluster_id);
      }
    }
    for (auto &cluster_involved : span_cluster) {
      if (cluster_involved != main_cluster_id) {
        repair_span_cluster.push_back(cluster_involved);
      }
    }

    for (auto &cluster_id : repair_span_cluster) {
      std::vector<std::pair<std::pair<std::string, int>, int>>
          blocks_to_read_in_cur_cluster;
      for (auto &live_block : live_blocks_in_group) {
        /*node_item &node = node_info_[live_block.first];
        if (node.cluster_id == cluster_id) {
          blocks_to_read_in_cur_cluster.push_back(
              {{node.ip, node.port}, live_block.second});

          node.network_cost += 1;
        }*/
        if(live_block.second < k){
          node_item &node = node_info_[live_block.first];
          if (node.cluster_id == cluster_id) {
            blocks_to_read_in_cur_cluster.push_back(
                {{node.ip, node.port}, live_block.second});
          }
        }else{
          cache_node_item &cache_node = cache_node_info_[live_block.first];
          if (cache_node.cluster_id == cluster_id){
            blocks_to_read_in_cur_cluster.push_back(              
                {{cache_node.ip, cache_node.port}, live_block.second});
          }

        }
      }
      /*if (cluster_id == main_cluster_id) {
        my_assert(blocks_to_read_in_cur_cluster.size() > 0);
      }*/
      blocks_to_read_in_each_cluster.push_back(blocks_to_read_in_cur_cluster);
    }
  } else{
    ///修复(k+g-1)~(k+g+real_l-1)所有有缓存的block【1个global & real_l个local block】
    /************从stripe cache_nodes读***************************/
    int cache_nodes_index =  failed_block_index - (k + g - 1);
    cache_node_item &live_cache_node = cache_node_info_
                                        [stripe.cache_nodes[cache_nodes_index]];
    /*全局块和备份放入cache node的块 按照规则是放置在同一个cluster中的*/
    unsigned int cache_cluster_id = live_cache_node.cluster_id;
    //只涉及main__cluster；无需repair_span_cluster.push_back(cache_cluster_id);
    assert(cache_cluster_id == main_cluster_id);

    std::vector<std::pair<std::pair<std::string, int>, int>>
            blocks_to_read_in_main_cluster;
   
    blocks_to_read_in_main_cluster.push_back(
                                      {{live_cache_node.ip,live_cache_node.port}, failed_block_index});
    blocks_to_read_in_each_cluster.push_back(blocks_to_read_in_main_cluster);
    //live_blocks_in_each_cluster[live_node.cluster_id].push_back(i);


  }
}



void Coordinator::generate_repair_plan(
    unsigned int stripe_id, std::vector<int> &failed_block_indexes,
    std::vector<std::vector<std::pair<std::pair<std::string, int>, int>>>
        &blocks_to_read_in_each_cluster,
    std::vector<unsigned int> &repair_span_cluster,
    std::vector<std::pair<unsigned int, int>> &new_locations_with_block_index) {
  stripe_item &stripe = stripe_info_[stripe_id];
  int k = stripe.k;
  int real_l = stripe.real_l;
  int g = stripe.g;
  int b = stripe.b;

  int failed_block_index = failed_block_indexes[0];
  node_item &failed_node = node_info_[stripe.nodes[failed_block_index]];
  unsigned int main_cluster_id = failed_node.cluster_id;
  repair_span_cluster.push_back(main_cluster_id);

  // 将修复好的块放回原位
  new_locations_with_block_index.push_back(
      {failed_node.node_id, failed_block_index});

  my_assert(failed_block_index >= 0 &&
            failed_block_index <= (k + g + real_l - 1));
  if (failed_block_index >= k && failed_block_index <= (k + g - 1)) {
    // 修复全局校验块
    std::unordered_map<unsigned int, std::vector<int>>
        live_blocks_in_each_cluster;
    // 找到每个cluster中的存活块
    for (int i = 0; i <= (k + g - 1); i++) {
      if (i != failed_block_index) {
        node_item &live_node = node_info_[stripe.nodes[i]];
        live_blocks_in_each_cluster[live_node.cluster_id].push_back(i);
      }
    }

    std::unordered_map<unsigned int, std::vector<int>>
        live_blocks_needed_in_each_cluster;
    int num_of_needed_live_blocks = k;
    // 优先读取main cluster的,即损坏块所在cluster
    for (auto live_block_index : live_blocks_in_each_cluster[main_cluster_id]) {
      if (num_of_needed_live_blocks <= 0) {
        break;
      }
      live_blocks_needed_in_each_cluster[main_cluster_id].push_back(
          live_block_index);
      num_of_needed_live_blocks--;
    }

    // 需要对剩下的cluster中存活块的数量进行排序,优先从存活块数量多的cluster中读取
    std::vector<std::pair<unsigned int, std::vector<int>>>
        sorted_live_blocks_in_each_cluster;
    for (auto &cluster : live_blocks_in_each_cluster) {
      if (cluster.first != main_cluster_id) {
        sorted_live_blocks_in_each_cluster.push_back(
            {cluster.first, cluster.second});
      }
    }
    std::sort(sorted_live_blocks_in_each_cluster.begin(),
              sorted_live_blocks_in_each_cluster.end(), cmp_num_live_blocks);
    for (auto &cluster : sorted_live_blocks_in_each_cluster) {
      for (auto &block_index : cluster.second) {
        if (num_of_needed_live_blocks <= 0) {
          break;
        }
        live_blocks_needed_in_each_cluster[cluster.first].push_back(
            block_index);
        num_of_needed_live_blocks--;
      }
    }

    // 记录需要从main cluster中读取的存活块
    std::vector<std::pair<std::pair<std::string, int>, int>>
        blocks_to_read_in_main_cluster;
    for (auto &block_index :
         live_blocks_needed_in_each_cluster[main_cluster_id]) {
      node_item &node = node_info_[stripe.nodes[block_index]];
      blocks_to_read_in_main_cluster.push_back(
          {{node.ip, node.port}, block_index});

      node.network_cost += 1;
    }
    blocks_to_read_in_each_cluster.push_back(blocks_to_read_in_main_cluster);

    // 记录需要从其它cluster中读取的存活块
    for (auto &cluster : live_blocks_needed_in_each_cluster) {
      if (cluster.first != main_cluster_id) {
        repair_span_cluster.push_back(cluster.first);

        std::vector<std::pair<std::pair<std::string, int>, int>>
            blocks_to_read_in_another_cluster;
        for (auto &block_index : cluster.second) {
          node_item &node = node_info_[stripe.nodes[block_index]];
          blocks_to_read_in_another_cluster.push_back(
              {{node.ip, node.port}, block_index});

          node.network_cost += 1;
        }
        blocks_to_read_in_each_cluster.push_back(
            blocks_to_read_in_another_cluster);
      }
    }
  } else {
    // 修复数据块和局部校验块
    int group_index = -1;
    if (failed_block_index >= 0 && failed_block_index <= (k - 1)) {
      group_index = failed_block_index / b;
    } else {
      group_index = failed_block_index - (k + g);
    }

    std::vector<std::pair<unsigned int, int>> live_blocks_in_group;
    for (int i = 0; i < b; i++) {
      int block_index = group_index * b + i;
      if (block_index != failed_block_index) {
        if (block_index >= k) {
          break;
        }
        live_blocks_in_group.push_back(
            {stripe.nodes[block_index], block_index});
      }
    }
    if (failed_block_index != k + g + group_index) {
      live_blocks_in_group.push_back(
          {stripe.nodes[k + g + group_index], k + g + group_index});
    }

    std::unordered_set<unsigned int> span_cluster;
    for (auto &live_block : live_blocks_in_group) {
      span_cluster.insert(node_info_[live_block.first].cluster_id);
    }
    for (auto &cluster_involved : span_cluster) {
      if (cluster_involved != main_cluster_id) {
        repair_span_cluster.push_back(cluster_involved);
      }
    }

    for (auto &cluster_id : repair_span_cluster) {
      std::vector<std::pair<std::pair<std::string, int>, int>>
          blocks_to_read_in_cur_cluster;
      for (auto &live_block : live_blocks_in_group) {
        node_item &node = node_info_[live_block.first];
        if (node.cluster_id == cluster_id) {
          blocks_to_read_in_cur_cluster.push_back(
              {{node.ip, node.port}, live_block.second});

          node.network_cost += 1;
        }
      }
      /*if (cluster_id == main_cluster_id) {
        my_assert(blocks_to_read_in_cur_cluster.size() > 0);
      }*/
      blocks_to_read_in_each_cluster.push_back(blocks_to_read_in_cur_cluster);
    }
  }
}









