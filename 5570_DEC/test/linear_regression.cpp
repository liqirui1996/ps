#include <vector>
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "gtest/gtest.h"

#include "driver/engine.hpp"
#include "lib/batchiterator.hpp"
#include "lib/data_loader.hpp"
#include "lib/kdd_sample.hpp"
#include "worker/kv_client_table.hpp"


// Define arguments
DEFINE_int32(my_id, -1, "the process id of this program");
DEFINE_string(config_file, "", "The config file path");
// Data loading config
DEFINE_string(hdfs_namenode, "proj10", "The hdfs namenode hostname");
DEFINE_int32(hdfs_namenode_port, 9000, "The hdfs port");
DEFINE_int32(hdfs_master_port, 23489, "A port number for the hdfs assigner host");
DEFINE_int32(n_loaders_per_node, 1, "The number of loaders per node");
DEFINE_string(input, "", "The hdfs input url");
DEFINE_int32(n_features, 10, "The number of feature in the dataset");
// Traing config
DEFINE_int32(n_workers_per_node, 1, "The number of workers per node");
DEFINE_int32(n_iters, 10, "The number of interattions");
DEFINE_int32(batch_size, 100, "Batch size");
DEFINE_double(alpha, 0.001, "learning rate");

namespace csci5570 {

std::vector<double> compute_gradients(const std::vector<lib::KddSample>& samples, const std::vector<Key>& keys,
                                      const std::vector<double>& vals, double alpha) {
  std::vector<double> deltas(keys.size(), 0.);
  for (auto sample : samples) {
    auto& x = sample.x_;
    double y = sample.y_;
    double predict = 0.;
    int idx = 0;
    for (auto& field : x) {
      while (keys[idx] < field.first)
        ++idx;
      predict += vals[idx] * field.second;
    }
    predict += vals.back();
    int predictLabel;
    if (predict >= 0) {
      predictLabel = 1;
    } else {
      predictLabel = -1;
    }
    idx = 0;
    for (auto& field : x) {
      while (keys[idx] < field.first)
        ++idx;
      deltas[idx] += alpha * field.second * (y - predictLabel);
    }
    deltas[deltas.size() - 1] += alpha * (y - predictLabel);
  }
  return deltas;
}

double correct_rate(const std::vector<lib::KddSample>& samples, const std::vector<Key>& keys,
                    const std::vector<double>& vals) {
  int total = samples.size();
  double n = 0;
  for (auto sample : samples) {
    auto& x = sample.x_;
    double y = sample.y_;
    double predict = 0.;

    int idx = 0;
    for (auto& field : x) {
      while (keys[idx] < field.first)
        ++idx;
      predict += vals[idx] * field.second;
    }
    predict += vals.back();

    int predict_;
    if (predict >= 0) {
      predict_ = 1;
    } else {
      predict_ = -1;
    }
    if (predict_ == y) {
      n++;
    }
  }

  double result = n / total;
  return result;
}

void LrTest(uint32_t node_id) {
  // Load Data
  using DataStore = std::vector<lib::KddSample>;
  using Parser = lib::Parser<lib::KddSample, DataStore>;
  using Parse = std::function<lib::KddSample(boost::string_ref, int)>;

  DataStore data_store;
  lib::KddSample kdd_sample;

  auto kdd_parse = Parser::parse_kdd;
  int n_features = 10;
  std::string url = "hdfs:///datasets/classification/kdd12"; 
  std::string hdfs_namenode = "proj10";                       
  std::string master_host = "proj"+std::to_string(node_id+5);                         
  std::string worker_host = "proj"+std::to_string(node_id+5);                         
  int hdfs_namenode_port = 9000;                              
  int master_port = 45743;                                    
  lib::DataLoader<lib::KddSample, DataStore> data_loader;
  data_loader.load<Parse>(url, hdfs_namenode, master_host, worker_host, hdfs_namenode_port, master_port, n_features, kdd_parse, &data_store);
  

  // Start Engine
  uint32_t n=node_id;
  Node node{n, "proj"+std::to_string(n+5), 23847};
  std::vector<Node> nodes;
  for(uint32_t i=0;i<2;i++){
    Node nodet{i, "proj"+std::to_string(i+5), 23847};
    nodes.push_back(nodet);
  }
  LOG(INFO)<<node.hostname;
  Engine engine(node, nodes);
  engine.StartEverything();

  // Create table on the server side
  const auto kTable = engine.CreateTable<double>(ModelType::ASP, StorageType::Map);

  // Specify task
  MLTask task;
  task.SetTables({kTable});
  task.SetWorkerAlloc({{0, 5},{1,5}});
  // get client table
  // Before learning
  LOG(INFO) << "Before learning";
  task.SetLambda([kTable, &data_store](const Info& info) {
    BatchIterator<lib::KddSample> batch(data_store);
    auto keys_data = batch.NextBatch(2000);
    std::vector<lib::KddSample> datasample = keys_data.second;
    auto keys = keys_data.first;
    std::vector<double> vals;
    KVClientTable<double> table(info.thread_id, kTable, info.send_queue,
                                info.partition_manager_map.find(kTable)->second, info.callback_runner);
    table.Get(keys, &vals);
    auto correctrate = correct_rate(datasample, keys, vals);
    LOG(INFO) << correctrate;
  });

  engine.Run(task);
  LOG(INFO) << "Learning";
  task.SetLambda([kTable, &data_store](const Info& info) {

    BatchIterator<lib::KddSample> batch(data_store);
    for (int iter = 0; iter < 5; ++iter) {
      auto keys_data = batch.NextBatch(2000);
      std::vector<lib::KddSample> datasample = keys_data.second;
      auto keys = keys_data.first;
      std::vector<double> vals;
      KVClientTable<double> table(info.thread_id, kTable, info.send_queue,
                                  info.partition_manager_map.find(kTable)->second, info.callback_runner);
      table.Get(keys, &vals);
      auto delta = compute_gradients(datasample, keys, vals, 0.1);
      table.Add(keys, delta);
    }
  });
  engine.Run(task);
  LOG(INFO) << "After training";
  task.SetLambda([kTable, &data_store](const Info& info) {
    BatchIterator<lib::KddSample> batch(data_store);
    auto keys_data = batch.NextBatch(1000);
    std::vector<lib::KddSample> datasample = keys_data.second;
    auto keys = keys_data.first;
    std::vector<double> vals;
    KVClientTable<double> table(info.thread_id, kTable, info.send_queue,
                                info.partition_manager_map.find(kTable)->second, info.callback_runner);
    table.Get(keys, &vals);
    auto correctrate = correct_rate(datasample, keys, vals);
    LOG(INFO) << correctrate;
  });

  engine.Run(task);

  engine.StopEverything();

}
}  // namespace csci5570

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_stderrthreshold = 0;
  FLAGS_colorlogtostderr = true;
  uint32_t node_id;
  node_id=atoi(argv[1]);
  csci5570::LrTest(node_id);
}
