#include "mdrpt.hpp"
#include <cblas.h>
#include <stdio.h>
#include "drpt.hpp"
#include "../math/matrix_multiply.hpp"
#include <vector>
#include <random>
#include <mpi.h>
#include <string>
#include <iostream>
#include <omp.h>
#include <map>
#include <unordered_map>
#include <fstream>
#include "../algo/drpt_global.hpp"
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <limits.h>
#include <cstring>


using namespace std;
using namespace std::chrono;

dmrpt::MDRPT::MDRPT(int ntrees, int algo, vector <vector<VALUE_TYPE>> original_data, int tree_depth,
                    double tree_depth_ratio,
                    int total_data_set_size,
                    int rank, int world_size, string input_path, string output_path) {
    this->data_dimension = original_data[0].size();
    this->tree_depth = tree_depth;
    this->original_data = original_data;
    this->total_data_set_size = total_data_set_size;
    this->rank = rank;
    this->world_size = world_size;
    this->ntrees = ntrees;
    this->algo = algo;
    this->input_path = input_path;
    this->output_path = output_path;
    this->tree_depth_ratio = tree_depth_ratio;
    this->trees_leaf_all = vector < vector < vector < DataPoint > >>(ntrees);
}

template<typename T> vector <T> slice(vector < T >
const &v,
int m,
int n
) {
auto first = v.cbegin() + m;
auto last = v.cbegin() + n + 1;

std::vector <T> vec(first, last);
return
vec;
}


template<typename T> bool allEqual(std::vector < T >
const &v) {
return
std::adjacent_find(v
.

begin(), v

.

end(), std::not_equal_to<T>()

) == v.

end();

}

void dmrpt::MDRPT::grow_trees(float density) {

    char results[500];
    char hostname[HOST_NAME_MAX];
    int host = gethostname(hostname, HOST_NAME_MAX);
    string file_path_stat = output_path + "stats_divided.txt.";
    std::strcpy(results, file_path_stat.c_str());
    std::strcpy(results + strlen(file_path_stat.c_str()), hostname);

    ofstream fout(results, std::ios_base::app);


    dmrpt::MathOp mathOp;
    VALUE_TYPE *imdataArr = mathOp.convert_to_row_major_format(this->original_data);

    int rows = this->original_data[0].size();
    int cols = this->original_data.size();

    int global_tree_depth = this->tree_depth * this->tree_depth_ratio;
    int local_tree_depth = this->tree_depth - global_tree_depth;

    auto start_matrix_index = high_resolution_clock::now();

    VALUE_TYPE *B = mathOp.build_sparse_projection_matrix(this->rank, this->world_size, this->data_dimension,
                                                          global_tree_depth * this->ntrees, density);

    // P= X.R
    VALUE_TYPE *P = mathOp.multiply_mat(imdataArr, B, this->data_dimension, global_tree_depth * this->ntrees, cols,
                                        1.0);

    auto stop_matrix_index = high_resolution_clock::now();

    auto matrix_time = duration_cast<microseconds>(stop_matrix_index - start_matrix_index);


    auto start_grow_index = high_resolution_clock::now();

    int starting_index = (this->total_data_set_size / world_size) * this->rank;


    this->drpt_global = dmrpt::DRPTGlobal(P, B, cols, global_tree_depth, this->original_data, this->ntrees,
                                          starting_index,
                                          this->total_data_set_size, this->rank, this->world_size, input_path,
                                          output_path);


    cout << " rank " << rank << " starting growing trees" << endl;
    this->drpt_global.grow_global_tree();
    auto stop_grow_index = high_resolution_clock::now();
    auto index_time = duration_cast<microseconds>(stop_grow_index - start_grow_index);

    cout << " rank " << rank << " completing growing trees" << endl;

    cout << " rank " << rank << " running  datapoint collection " << endl;

    this->drpt_global.calculate_tree_leaf_correlation();

    auto start_collect = high_resolution_clock::now();

    vector < vector < vector < DataPoint>>> leaf_nodes_of_trees(ntrees);
    int total_child_size = (1 << (this->tree_depth)) - (1 << (this->tree_depth - 1));

    for (int i = 0; i < ntrees; i++) {
        this->trees_leaf_all[i] = vector < vector < dmrpt::DataPoint >> (total_child_size);
        leaf_nodes_of_trees[i] = this->drpt_global.collect_similar_data_points(i, true);
    }
    auto stop_collect = high_resolution_clock::now();
    auto collect_time = duration_cast<microseconds>(stop_collect - start_collect);


    cout << " rank " << rank << " similar datapoint collection completed" << endl;


    int total_leaf_size = (1 << (this->tree_depth)) - (1 << (this->tree_depth - 1));

    int leafs_per_node = total_leaf_size / this->world_size;

    cout << " leafs per node " << leafs_per_node << endl;

    int my_start_count = 0;
    int my_end_count = 0;

    //large trees
    if (total_leaf_size >= this->world_size) {
        my_start_count = leafs_per_node * this->rank;
        if (this->rank < this->world_size - 1) {
            my_end_count = leafs_per_node * (this->rank + 1);
        } else {
            my_end_count = total_leaf_size;
        }
    }

    cout << " start count " << my_start_count << " end count " << my_end_count << endl;

    auto start_collect_local = high_resolution_clock::now();

    for (int i = 0; i < ntrees; i++) {
        vector <vector<DataPoint>> leafs = leaf_nodes_of_trees[i];
        VALUE_TYPE *C = mathOp.build_sparse_projection_matrix(this->rank, this->world_size, this->data_dimension,
                                                              local_tree_depth, density);

        cout << " tree " << i << " projection matrix completed and leafs size " << leafs.size() << endl;

        int data_nodes_count_per_process = 0;

        for (int j = 0; j < leafs.size(); j++) {
//            cout<< " creating leaf " <<j<<endl;
            vector <vector<VALUE_TYPE>> local_data(leafs[j].size());
            for (int k = 0; k < leafs[j].size(); k++) {
                local_data[k] = leafs[j][k].image_data;
            }
//            cout<< " data filling complete for  leaf " <<j <<" size "<<local_data.size()<<endl;
            VALUE_TYPE *local_data_arr = mathOp.convert_to_row_major_format(local_data);
//            cout<< " row major version completed " <<j<<endl;

            VALUE_TYPE *LP = mathOp.multiply_mat(local_data_arr, C, this->data_dimension,
                                                 local_tree_depth,
                                                 leafs[j].size(), 1.0);
//            cout<<" creating drpt "<< j <<leafs.size()<<endl;
            DRPT drpt1 = dmrpt::DRPT(LP, C, leafs[j].size(),
                                     local_tree_depth, local_data, 1, starting_index, this->rank, this->world_size);

            drpt1.grow_local_tree();
//            cout<<" creating drpt "<< j <<" tree growing completed"<<endl;

            vector <vector<int>> final_clustered_data = drpt1.get_all_leaf_node_indices(0);
//            cout << " final_clustered_data size for leaf " << j << final_clustered_data.size() << endl;

            for (int l = 0; l < final_clustered_data.size(); l++) {
                vector <DataPoint> data_vec;
                for (int m = 0; m < final_clustered_data[l].size(); m++) {
                    int index = final_clustered_data[l][m];
                    int real_index = leafs[j][index].index;
                    data_vec.push_back(leafs[j][index]);
                }

                int id = my_start_count + (data_nodes_count_per_process % leafs_per_node);
                this->trees_leaf_all[i][id] = data_vec;

                data_nodes_count_per_process++;


            }

            free(local_data_arr);
            free(LP);


        }
        free(C);

    }

    auto end_collect_local = high_resolution_clock::now();
    auto collect_time_local = duration_cast<microseconds>(start_collect_local - end_collect_local);

    fout << rank << " matrix  " << matrix_time.count() << " global tree " << index_time.count() << " communication "
         << collect_time.count() << " local index growing  " << collect_time_local.count()
         << endl;


}

void dmrpt::MDRPT::calculate_nns(map<int, vector<dmrpt::DataPoint> > &local_nns, int tree, int nn) {

    dmrpt::MathOp mathOp;

    int total_leaf_size = (1 << (this->tree_depth)) - (1 << (this->tree_depth - 1));

    int leafs_per_node = total_leaf_size / this->world_size;

    int my_start_count = 0;
    int end_count = 0;

    //large trees
    if (total_leaf_size >= this->world_size) {
        my_start_count = leafs_per_node * this->rank;
        if (this->rank < this->world_size - 1) {
            end_count = leafs_per_node * (this->rank + 1);
        } else {
            end_count = total_leaf_size;
        }
    }


    for (int i = my_start_count; i < end_count; i++) {

        vector <DataPoint> data_points = this->trees_leaf_all[tree][i];

        for (int k = 0; k < data_points.size(); k++) {
            vector <DataPoint> vec(data_points.size());
#pragma omp parallel for
            for (int j = 0; j < data_points.size(); j++) {

                VALUE_TYPE distance = mathOp.calculate_distance(data_points[k].image_data,
                                                                data_points[j].image_data);

                DataPoint dataPoint;
                dataPoint.src_index = data_points[k].index;
                dataPoint.index = data_points[j].index;
                dataPoint.distance = distance;
                vec[j] = dataPoint;

            }

            sort(vec.begin(), vec.end(),
                 [](const DataPoint &lhs, const DataPoint &rhs) {
                     return lhs.distance < rhs.distance;
                 });

            vector <DataPoint> sub_vec;
            if (vec.size() > nn) {
                sub_vec = slice(vec, 0, nn - 1);
            } else {
                sub_vec = vec;
            }

            int idx = sub_vec[0].src_index;
            if (local_nns.find(idx) == local_nns.end()) {

                local_nns.insert(pair < int, vector < dmrpt::DataPoint >> (idx, sub_vec));
            } else {
                std::vector<int> dst;
                auto it = local_nns.find(idx);
                std::merge(it->second.begin(), it->second.end(), sub_vec.begin(),
                           sub_vec.end(), std::back_inserter(dst));
                dst.erase(unique(dst.begin(), dst.end(),
                                 [](const DataPoint &lhs,
                                    const DataPoint &rhs) {
                                     return lhs.index == rhs.index;
                                 }), dst.end());
                it->second = dst;
            }
        }
    }
}

std::map<int, vector < dmrpt::DataPoint>>

dmrpt::MDRPT::gather_nns(int nn) {

    cout << "gathering started " << endl;
    char results[500];

    char hostname[HOST_NAME_MAX];

    gethostname(hostname, HOST_NAME_MAX);
    string file_path_stat = output_path + "stats_divided.txt.";
    std::strcpy(results, file_path_stat.c_str());
    std::strcpy(results + strlen(file_path_stat.c_str()), hostname);

    ofstream fout(results, std::ios_base::app);


//    string file_path_distance = output_path + "distance_distribution" + to_string(rank)+ ".txt";
//    std::strcpy(results, file_path_distance.c_str());
//    std::strcpy(results + strlen(file_path_distance.c_str()), hostname);
//
//    ofstream fout1(results, std::ios_base::app);

    auto start_distance = high_resolution_clock::now();

    int chunk_size = this->total_data_set_size / this->world_size;

    int last_chunk_size = this->total_data_set_size - chunk_size * (this->world_size - 1);


    int my_chunk_size = chunk_size;
    int my_starting_index = this->rank * chunk_size;

    int my_end_index = 0;
    if (this->rank < this->world_size - 1) {
        my_end_index = (this->rank + 1) * chunk_size;
    } else {
        my_end_index = this->total_data_set_size;
        my_chunk_size = last_chunk_size;
    }


    std::map<int, vector<DataPoint>> local_nn_map;


    for (int i = 0; i < ntrees; i++) {
        this->calculate_nns(local_nn_map, i, 2 * nn);
    }


    cout << " rank " << rank << " distance calculation completed " << endl;

    auto stop_distance = high_resolution_clock::now();
    auto distance_time = duration_cast<microseconds>(stop_distance - start_distance);


    auto start_query = high_resolution_clock::now();

    cout << " rank " << rank << " map size" << local_nn_map.size() << endl;


    int *indices_count_per_process = new int[this->world_size]();
    int *indices_count_per_process_recev = new int[this->world_size]();

    int *indices_per_process = new int[local_nn_map.size()]();
    int *disps_indices_per_process = new int[this->world_size]();
    int *disps_indices_per_process_receiv = new int[this->world_size]();


    int *nn_indices_count_per_process = new int[local_nn_map.size()]();


    vector <vector<int>> indices_for_processes(this->world_size);

    for (int i = 0; i < this->world_size; i++) {
        indices_for_processes[i] = vector<int>();
    }


    for (auto it = local_nn_map.begin(); it != local_nn_map.end(); ++it) {
        int key = it->first;
        int process = key / chunk_size;
        vector <dmrpt::DataPoint> nn_data = it->second;
        indices_for_processes[process].push_back(key);
        indices_count_per_process[process] = indices_count_per_process[process] + 1;
    }

    MPI_Alltoall(indices_count_per_process, 1, MPI_INT, indices_count_per_process_recev, 1,
                 MPI_INT, MPI_COMM_WORLD);


    int total_indices_count_receving = 0;
    int count = 0;
    int total_nns_send = 0;
    for (int i = 0; i < this->world_size; i++) {
        total_indices_count_receving += indices_count_per_process_recev[i];
        for (int j = 0; j < indices_for_processes[i].size(); j++) {
            indices_per_process[count] = indices_for_processes[i][j];
            nn_indices_count_per_process[count] = local_nn_map[indices_for_processes[i][j]].size();
            total_nns_send += local_nn_map[indices_for_processes[i][j]].size();
            count++;
        }

        disps_indices_per_process[i] = (i > 0) ? (disps_indices_per_process[i - 1] + indices_count_per_process[i - 1])
                                               : 0;
        disps_indices_per_process_receiv[i] = (i > 0) ? (disps_indices_per_process_receiv[i - 1] +
                                                         indices_count_per_process_recev[i - 1]) : 0;
    }


    int *indices_per_process_receive = new int[total_indices_count_receving]();

    MPI_Alltoallv(indices_per_process, indices_count_per_process, disps_indices_per_process, MPI_INT,
                  indices_per_process_receive,
                  indices_count_per_process_recev, disps_indices_per_process_receiv, MPI_INT, MPI_COMM_WORLD);


    int *nn_indices_count_per_process_recev = new int[total_indices_count_receving]();

    MPI_Alltoallv(nn_indices_count_per_process, indices_count_per_process, disps_indices_per_process, MPI_INT,
                  nn_indices_count_per_process_recev,
                  indices_count_per_process_recev, disps_indices_per_process_receiv, MPI_INT, MPI_COMM_WORLD);


    int final_receiving_nns_count = 0;

    for (int m = 0; m < total_indices_count_receving; m++) {
        final_receiving_nns_count += nn_indices_count_per_process_recev[m];
    }

    cout << " rank " << rank << final_receiving_nns_count << endl;

    int *nn_indices_send = new int[total_nns_send]();
    int *nn_indices_receive = new int[final_receiving_nns_count]();

    VALUE_TYPE *nn_distance_send = new VALUE_TYPE[total_nns_send]();
    VALUE_TYPE *nn_distance_receive = new VALUE_TYPE[final_receiving_nns_count]();

    int *nn_indices_send_count = new int[this->world_size]();
    int *disps_nn_indices_send = new int[this->world_size]();
    int *nn_indices_recieve_count = new int[this->world_size]();
    int *disps_nn_indices_recieve = new int[this->world_size]();


    int nn_indices_send_index = 0;
    for (int i = 0; i < this->world_size; i++) {
        for (int j = 0; j < indices_for_processes[i].size(); j++) {
            vector <DataPoint> nn_indices = local_nn_map[indices_for_processes[i][j]];
            nn_indices_send_count[i] += nn_indices.size();
            for (int k = 0; k < nn_indices.size(); k++) {
                nn_indices_send[nn_indices_send_index] = nn_indices[k].index;
                nn_distance_send[nn_indices_send_index] = nn_indices[k].distance;
                nn_indices_send_index++;
            }
        }
        disps_nn_indices_send[i] = (i > 0) ? (disps_nn_indices_send[i - 1] + nn_indices_send_count[i - 1]) : 0;


        for (int l = 0; l < indices_count_per_process_recev[i]; l++) {
            int offset = disps_indices_per_process_receiv[i];
            int id = l + offset;
            nn_indices_recieve_count[i] += nn_indices_count_per_process_recev[id];
        }
        disps_nn_indices_recieve[i] = (i > 0) ? (disps_nn_indices_recieve[i - 1] + nn_indices_recieve_count[i - 1]) : 0;


    }


    MPI_Alltoallv(nn_indices_send, nn_indices_send_count, disps_nn_indices_send, MPI_INT, nn_indices_receive,
                  nn_indices_recieve_count, disps_nn_indices_recieve, MPI_INT, MPI_COMM_WORLD);

    MPI_Alltoallv(nn_distance_send, nn_indices_send_count, disps_nn_indices_send, MPI_VALUE_TYPE, nn_distance_receive,
                  nn_indices_recieve_count, disps_nn_indices_recieve, MPI_VALUE_TYPE, MPI_COMM_WORLD);


    std::map<int, vector<DataPoint>> final_nn_map;


    int nn_index = 0;
    for (int i = 0; i < total_indices_count_receving; i++) {
        int src_index = indices_per_process_receive[i];
        int nn_count = nn_indices_count_per_process_recev[i];
        for (int j = 0; j < nn_count; j++) {
            int nn_indi = nn_indices_receive[nn_index];
            VALUE_TYPE distance = nn_distance_receive[nn_index];
            DataPoint dataPoint;
            dataPoint.src_index = src_index;
            dataPoint.index = nn_indi;
            dataPoint.distance = distance;
            vector <DataPoint> vec;
            vec.push_back(dataPoint);
            if (final_nn_map.find(src_index) == final_nn_map.end()) {
                final_nn_map.insert(pair < int, vector < DataPoint >> (src_index, vec));
            } else {
                final_nn_map[src_index].insert(final_nn_map[src_index].end(), dataPoint);
                sort(final_nn_map[src_index].begin(), final_nn_map[src_index].end(),
                     [](const DataPoint &lhs, const DataPoint &rhs) {
                         return lhs.distance < rhs.distance;
                     });
                final_nn_map[src_index].erase(unique(final_nn_map[src_index].begin(), final_nn_map[src_index].end(),
                                                     [](const DataPoint &lhs,
                                                        const DataPoint &rhs) {
                                                         return lhs.index == rhs.index;
                                                     }), final_nn_map[src_index].end());
            }
            nn_index++;
        }
    }


    auto stop_query = high_resolution_clock::now();
    auto query_time = duration_cast<microseconds>(stop_query - start_query);

    fout << " distance calculation " << distance_time.count() << " communication time " << query_time.count() << endl;


    free(indices_count_per_process);
    free(indices_count_per_process_recev);
    free(indices_per_process);
    free(disps_indices_per_process);
    free(disps_indices_per_process_receiv);
    free(indices_per_process_receive);
    free(nn_indices_send);
    free(nn_indices_receive);
    free(nn_indices_recieve_count);
    free(disps_nn_indices_recieve);
    free(nn_distance_send);
    free(nn_distance_receive);
    return final_nn_map;
}


void dmrpt::MDRPT::communicate_nns(std::map<int, vector < dmrpt::DataPoint> &local_nns, int tree, int nn) {


    if(local_nns.find(5) != local_nns.end()) {
        local_nns[5]
        vector<int> target;
        std::copy_if( local_nns[5].begin(),  local_nns[5].end(), back_inserter(target),
                      [](dmrpt::DataPoint dataPoint){ return  dataPoint.distance < 5;});


    }


    int *sending_indices_count = new int[this->world_size]();
    int *receiving_indices_count = new int[this->world_size]();

    int send_count = local_nns.size();

    for (int i = 0; i < this->world_size; i++) {
        sending_indices_count[i] = send_count;
    }

    MPI_Alltoall(sending_indices_count, 1, MPI_INT, receiving_indices_count, 1, MPI_INT, MPI_COMM_WORLD);

    int total_receving = 0;

    int *disps_receiving_indices = new int[this->world_size]();
    int *disps_sending_indices = new int[this->world_size]();

    for (int i = 0; i < this->world_size; i++) {
        total_receving += receiving_indices_count[i];
        disps_sending_indices[i] = 0;
        disps_receiving_indices[i] = (i > 0) ? (disps_receiving_indices[i - 1] + receiving_indices_count[i - 1]) : 0;
    }

    int *sending_indices = new int[send_count]();
    VALUE_TYPE *sending_max_dist_thresholds = new VALUE_TYPE[send_count]();


    int count = 0;
    for (auto const &x: local_nns) {
        sending_indices[count] = x.first;
        sending_max_dist_thresholds[count] = x.second[nn - 1].distance;
        count++;
    }

    int *receiving_indices = new int[total_receving]();
    VALUE_TYPE *receiving_max_dist_thresholds = new VALUE_TYPE[total_receving]();

    MPI_Alltoallv(sending_indices, sending_indices_count, disps_sending_indices, MPI_INT, receiving_indices,
                  receiving_indices_count, disps_receiving_indices, MPI_INT, MPI_COMM_WORLD);

    MPI_Alltoallv(sending_max_dist_thresholds, sending_indices_count, disps_sending_indices, MPI_VALUE_TYPE,
                  receiving_max_dist_thresholds,
                  receiving_indices_count, disps_receiving_indices, MPI_VALUE_TYPE, MPI_COMM_WORLD);

    //we already gathered all the indices from all nodes and their respective max distance thresholds



    std::map<int, vector<int >> collected_dist_th_map; // key->indices value->ranks and threshold

    for (int i = 0; i < this->world_size; i++) {
        int amount = receiving_indices_count[i];
        int offset = disps_receiving_indices[i];
        for (int j = offset; j < (offset + amount); j++) {
            int index = receiving_indices[j];
            VALUE_TYPE dist_th = receiving_max_dist_thresholds[j];
            if (collected_dist_th_map.find(index) == collected_dist_th_map.end()) {
                vector<VALUE_TYPE> distanceThresholdVec(this->world_size, std::numeric_limits<VALUE_TYPE>::max());
                distanceThresholdVec[i] = dist_th;
                collected_dist_th_map.insert(pair < int, vector < int >> (index, distanceThresholdVec));
            } else {
                auto it = collected_dist_th_map.find(index);
                (it->second)[i] = dist_th;
            }
        }
    }

    vector <vector<int>> final_indices_allocation(this->world_size);

    for (auto const &it: collected_dist_th_map) {
        int min_rank = std::min_element((it.second).begin(), (it.second).end()) - (it.second).begin();
        final_indices_allocation[min_rank].push_back(it.first);
    }

    std::map<int, vector<VALUE_TYPE >> final_nn_sending_map;


    for (int i = 0; i < this->world_size; i++) {
        if (i != this->rank) {
            for (int j = 0; j < final_indices_allocation[i].size(); j++) {
                   int index = final_indices_allocation[i][j];
                   VALUE_TYPE dst_th = collected_dist_th_map[index][i];
                  if(local_nns.find(index) != local_nns.end()) {
                      vector<dmrpt::DataPoint> target;
                      std::copy_if(local_nns[index].begin(),  local_nns[index].end(), std::back_inserter(target),
                                    [dst_th](dmrpt::DataPoint dataPoint){ return  dataPoint.distance < dst_th;});


                  }

            }
        }
    }


}

