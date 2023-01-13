#ifndef _FILE_WRITER_H_#define _FILE_WRITER_H_#include <iostream>#include <fstream>#include <string>#include <vector>#include <sstream>#include "mpi.h"#include <cstring>#include <cmath>#include "dmrpt/algo/drpt_local.hpp"using namespace std;namespace dmrpt {	class FileWriter {		template<class IT>		void mpi_write_edge_list(map<int, vector<DataPoint>> &data_points,				string output_path, int local_rows, int endrow,int startrow,int global_rows, int global_clos, int rank, int world_size);		template<class IT>	    IT **alloc2d(int rows, int cols);	};#endif //_FILE_WRITER_H_