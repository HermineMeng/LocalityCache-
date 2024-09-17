#include "../include/erasure_code.h"
#include <algorithm>
#include <cstring>

void make_lrc_coding_matrix(int k, int g, int real_l, int *coding_matrix) {
  my_assert(k % real_l == 0);
  int num_of_data_block_per_group = k / real_l;

  int *matrix = reed_sol_vandermonde_coding_matrix(k, g + 1, 8);
  my_assert(matrix != nullptr && coding_matrix != nullptr);

  bzero(coding_matrix, sizeof(int) * k * (g + real_l));

  // 编码矩阵前g行是传统的范德蒙矩阵
  for (int i = 0; i < g; i++) {
    for (int j = 0; j < k; j++) {
      coding_matrix[i * k + j] = matrix[(i + 1) * k + j];
    }
  }

  // 编码矩阵后面real_l行用于生成局部校验块
  for (int i = 0; i < real_l; i++) {
    for (int j = 0; j < k; j++) {
      if (i * num_of_data_block_per_group <= j &&
          j < (i + 1) * num_of_data_block_per_group) {
        coding_matrix[(i + g) * k + j] = 1;
      }
    }
  }

  free(matrix);
}

void encode(int k, int g, int real_l, char **data, char **coding, int blocksize,
            Encode_Type encode_type) {
  if (encode_type == Encode_Type::Azure_LRC) {
    std::vector<int> coding_matrix(k * (g + real_l), 0);
    make_lrc_coding_matrix(k, g, real_l, coding_matrix.data());
    jerasure_matrix_encode(k, g + real_l, 8, coding_matrix.data(), data, coding,
                           blocksize);
  }
}


bool decode(int k, int g, int real_l, char **data, char **coding, 
            std::shared_ptr<std::vector<int>> erasures, 
            int blocksize, bool repair){
  std::vector<int> coding_matrix((g + real_l) * k, 0);
  make_lrc_coding_matrix(k, g, real_l, coding_matrix.data());
  if (!repair){
    if (check_k_data(*erasures, k)){
      return true;
    }
  }
        
  if (jerasure_matrix_decode(k, g + real_l, 8, coding_matrix.data(), 
                             0, erasures->data(), data, coding, blocksize) == -1){
    std::vector<int> new_erasures(g + real_l + 1, 1);
    int survival_number = k + g + real_l - erasures->size() + 1;
    std::vector<int> survival_index;
    auto part_new_erasure = std::make_shared<std::vector<std::vector<int>>>();
    for (int i = 0; i < int(erasures->size() - 1); i++){
      new_erasures[i] = (*erasures)[i];
    }
    new_erasures[g + real_l] = -1;

    for (int i = 0; i < k + g + real_l; i++){
      if (std::find(erasures->begin(), erasures->end(), i) == erasures->end()){
        survival_index.push_back(i);
      }
    }
    if (survival_number > k){
      combine(part_new_erasure, survival_index.size(), survival_number - k);
    }
    for (int i = 0; i < int(part_new_erasure->size()); i++){
      for (int j = 0; j < int((*part_new_erasure)[i].size()); j++){
        new_erasures[erasures->size() - 1 + j] = survival_index[(*part_new_erasure)[i][j] - 1];
      }

      if (jerasure_matrix_decode(k, g + real_l, 8, coding_matrix.data(), 0, new_erasures.data(), data, coding, blocksize) != -1){
        return true;
        break;
      }
    }
  }else{

    return true;
  }
  return false;
  
}



bool check_received_block(int k, int expect_block_number, 
                          std::shared_ptr<std::vector<int>> blocks_idx_ptr, int blocks_ptr_size){

  
  if (blocks_ptr_size != -1){
    if (int(blocks_idx_ptr->size()) != blocks_ptr_size){
      return false;
    }
  }

  if (int(blocks_idx_ptr->size()) >= expect_block_number){
    return true;
  } else if (int(blocks_idx_ptr->size()) == k){ // azure_lrc防误杀
      for (int i = 0; i < k; i++){
      // 没找到
        if (std::find(blocks_idx_ptr->begin(), blocks_idx_ptr->end(), i) == blocks_idx_ptr->end()){
          return false;
        }
      }
  } else{
    return false;
  }
  
  return true;
  
}


bool check_k_data(std::vector<int> erasures, int k){
  int flag = 1;
  for (int i = 0; i < k; i++){
    if (std::find(erasures.begin(), erasures.end(), i) != erasures.end()){
      flag = 0;
    }
  }
  if (flag){
    return true;
  }

  return false;
}


void dfs(std::vector<int> temp, std::shared_ptr<std::vector<std::vector<int>>> ans, int cur, int n, int k)
{
    if (int(temp.size()) + (n - cur + 1) < k)
    {
        return;
    }
    if (temp.size() == k)
    {
        ans->push_back(temp);
        return;
    }
    temp.push_back(cur);
    dfs(temp, ans, cur + 1, n, k);
    temp.pop_back();
    dfs(temp, ans, cur + 1, n, k);
}

bool combine(std::shared_ptr<std::vector<std::vector<int>>> ans, int n, int k)
{
    std::vector<int> temp;
    dfs(temp, ans, 1, n, k);
    return true;
}