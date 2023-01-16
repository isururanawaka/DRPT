#include <iostream>#include <fstream>#include <string>#include <vector>#include <sstream>#include "mpi.h"#include <cstring>#include <cmath>#include <map>#include "../algo/drpt_local.hpp"#include "file_writer.hpp"using namespace std;template<typename IT> IT** dmrpt::FileWriter<IT>::alloc2d(int rows, int cols){	int size = rows*cols;	IT *data = malloc(size*sizeof(IT));	IT **array = malloc(rows*sizeof(IT*));	for (int i=0; i<rows; i++)		array[i] = &(data[i*cols]);	return array;}template<typename IT>void dmrpt::FileWriter<IT>::mpi_write_edge_list(map<int, vector<dmrpt::DataPoint>> &data_points, string output_path, int  nn,		int rank, int world_size) {	MPI_Offset offset;	MPI_File   file;	MPI_Status status;	MPI_Datatype num_as_string;	MPI_Datatype localarray;	IT **data;	char *const fmt="%1d ";	char *const endfmt="\n";	const int charspernum=9;	int local_rows = data_points.size()*(nn-1);	int cols = 2;	int *local_total = new int[1] ();	int *global_total = new int[1] ();	local_total[0]=local_rows;	MPI_Allreduce (local_total, global_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);	int global_rows = global_total[0];	int startrow = rank * local_rows;	int endrow = startrow+local_rows*(nn-1);	data = alloc2d<IT>(local_rows,cols);	for (int i=0; i<data_points.size(); i++)	{		vector<DataPoint> vec = data_points[i];		int count = 0;		for (int j = 0; j < nn; j++)		{			if (vec[j].src_index != vec[j].index && count < (nn-1))			{				int ind = i*(nn-1)+count;				data[i][0] =  vec[j].src_index;				data[i][1] = vec[j].index;				count++;			}		}		if(count != nn -1){			cout<<"Wrong number of nns for index"<< vec[j].src_index<<endl;			exit(1);		}	}	MPI_Type_contiguous(charspernum, MPI_CHAR, &num_as_string);	MPI_Type_commit(&num_as_string);	/* convert our data into txt */	char *data_as_txt = malloc(local_rows*cols*charspernum*sizeof(char));	int count = 0;	for (int i=0; i<local_rows; i++) {		for (int j=0; j<cols-1; j++) {			sprintf(&data_as_txt[count*charspernum],data[i][j],fmt);			count++;		}		sprintf(&data_as_txt[count*charspernum],data[i][cols-1],fmt,);		count++;	}	printf("%d: %s\n", rank, data_as_txt);	/* create a type describing our piece of the array */	int globalsizes[2] = {global_rows, cols};	int localsizes [2] = {local_rows, cols};	int starts[2]      = {startrow, 0};	int order          = MPI_ORDER_C;	MPI_Type_create_subarray(2, globalsizes, localsizes, starts, order, num_as_string, &localarray);	MPI_Type_commit(&localarray);	/* open the file, and set the view */	MPI_File_open(MPI_COMM_WORLD, output_path,			MPI_MODE_CREATE|MPI_MODE_WRONLY,			MPI_INFO_NULL, &file);	MPI_File_set_view(file, 0,  MPI_CHAR, localarray,			"native", MPI_INFO_NULL);	MPI_File_write_all(file, data_as_txt, local_rows*cols, num_as_string, &status);	MPI_File_close(&file);	MPI_Type_free(&localarray);	MPI_Type_free(&num_as_string);	free(data[0]);	free(data);}template class dmrpt::FileWriter<int>;