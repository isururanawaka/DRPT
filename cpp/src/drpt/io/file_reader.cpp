#include "file_reader.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include "mpi.h"
#include <cstring>
#include <cmath>

using namespace std;


template<typename T> vector <T> slice (vector < T > const &v, int m,int n) {
   auto first = v.cbegin () + m;
   auto last = v.cbegin () + n + 1;
   std::vector <T> vec (first, last);
   return vec;
}

int drpt::ImageReader::reverse_int (int i)
{
  unsigned char ch1, ch2, ch3, ch4;
  ch1 = i & 255;
  ch2 = (i >> 8) & 255;
  ch3 = (i >> 16) & 255;
  ch4 = (i >> 24) & 255;
  return ((int) ch1 << 24) + ((int) ch2 << 16) + ((int) ch3 << 8) + ch4;
}

vector <vector<VALUE_TYPE>> drpt::ImageReader::read_ubyte (string path, int no_of_images,
		int dimension, int rank, int world_size)
{
  vector <vector<VALUE_TYPE>> arr;

  ifstream file (path, ios::binary);
  if (file.is_open ())
    {
      int magic_number = 0;
      int number_of_images = 0;
      int n_rows = 0;
      int n_cols = 0;
      file.read ((char *) &magic_number, sizeof (magic_number));
      magic_number = this->reverse_int (magic_number);
      file.read ((char *) &number_of_images, sizeof (number_of_images));
      number_of_images = this->reverse_int (number_of_images);
      file.read ((char *) &n_rows, sizeof (n_rows));
      n_rows = this->reverse_int (n_rows);
      file.read ((char *) &n_cols, sizeof (n_cols));
      n_cols = this->reverse_int (n_cols);

      int chunk_size = number_of_images / world_size;
      if (rank < world_size - 1)
        {
          arr.resize (chunk_size, vector<VALUE_TYPE> (dimension));
        }
      else if (rank == world_size - 1)
        {
          chunk_size = no_of_images - chunk_size * (world_size - 1);
          arr.resize (chunk_size, vector<VALUE_TYPE> (dimension));
        }

      for (int i = 0; i < number_of_images; ++i)
        {
          for (int r = 0; r < n_rows; ++r)
            {
              for (int c = 0; c < n_cols; ++c)
                {
                  unsigned char temp = 0;
                  file.read ((char *) &temp, sizeof (temp));
                  if (i >= rank * chunk_size and i < (rank + 1) * chunk_size and rank < world_size - 1)
                    {
                      arr[i - rank * chunk_size][(n_rows * r) + c] = (VALUE_TYPE) temp;
                    }
                  else if (rank == world_size - 1 && i >= (rank) * chunk_size)
                    {
                      arr[i - rank * chunk_size][(n_rows * r) + c] = (VALUE_TYPE) temp;
                    }
                }
            }
        }
    }
  file.close ();
  return arr;
}

vector <vector<VALUE_TYPE>>
drpt::ImageReader::read_mnist_labels (string path, int no_of_images, int dimension, int rank, int world_size)
{

  vector <vector<VALUE_TYPE>> arr;

  ifstream file (path, ios::binary);

  if (file.is_open ())
    {
      int magic_number = 0;
      int number_of_labels = 0;
      file.read ((char *) &magic_number, sizeof (magic_number));
      magic_number = this->reverse_int (magic_number);

      if (magic_number != 2049) throw runtime_error ("Invalid MNIST label file!");

      file.read ((char *) &number_of_labels, sizeof (number_of_labels)),
          number_of_labels = this->reverse_int (number_of_labels);

      int chunk_size = number_of_labels / world_size;
      if (rank < world_size - 1)
        {
          arr.resize (chunk_size, vector<VALUE_TYPE> (dimension));
        }
      else if (rank == world_size - 1)
        {
          chunk_size = no_of_images - chunk_size * (world_size - 1);
          arr.resize (chunk_size, vector<VALUE_TYPE> (dimension));
        }

      for (int i = 0; i < number_of_labels; ++i)
        {
          unsigned char temp = 0;
          file.read ((char *) &temp, sizeof (temp));
          if (i >= rank * chunk_size and i < (rank + 1) * chunk_size and rank < world_size - 1)
            {
              arr[i - rank * chunk_size][0] = (VALUE_TYPE) temp;
            }
          else if (rank == world_size - 1 && i >= (rank) * chunk_size)
            {
              arr[i - rank * chunk_size][0] = (VALUE_TYPE) temp;
            }
        }
      return arr;
    }
  else
    {
      throw runtime_error ("Unable to open file `" + path + "`!");
    }
}

vector<VALUE_TYPE> load_vector (istream &in)
{
  vector<VALUE_TYPE> v;
  string line;
  if (getline (in, line))
    {
      istringstream iss (line);
      VALUE_TYPE n;
      while (iss >> n)
        v.push_back (n);
    }
  return v;
}

vector <vector<VALUE_TYPE>>
drpt::ImageReader::read_File (string path, int no_of_data_points, int dimension, int rank, int world_size)
{

  vector <vector<VALUE_TYPE>> arr;
  int chunk_size = no_of_data_points / world_size;
  int starting_point = rank * chunk_size;
  int endpoint = (rank + 1) * chunk_size;
  if (rank < world_size - 1)
    {
      arr.resize (chunk_size, vector<VALUE_TYPE> (dimension));
    }
  else if (rank == world_size - 1)
    {
      chunk_size = no_of_data_points - chunk_size * (world_size - 1);
      arr.resize (chunk_size, vector<VALUE_TYPE> (dimension));
      endpoint = no_of_data_points;
    }

  ifstream file (path);
  if (file.is_open ())
    {
      string line;
      int count = 0;
      int index = 0;
      while (getline (file, line))
        {
          if (count >= starting_point && count < endpoint)
            {
              std::stringstream linestream (line);
              std::string data;
              VALUE_TYPE n;
              int di = 0;
              while (std::getline (linestream, data, ' '))
                {
                  arr[index][di] = atoi (data.c_str ());
                  di++;
                }
              index++;
            }
          count++;
        }

    }
  else
    {
      throw runtime_error ("Unable to open file `" + path + "`!");
    }
  return arr;
}

vector <vector<VALUE_TYPE>>
drpt::ImageReader::mpi_file_read (string path, int rank, int world_size, int overlap, long total_data_set_size,
                                   char delim, int dimension)
{
  MPI_Offset globalstart, globalend, filesize;
  MPI_File in;
  const char *cstr = path.c_str ();
  int ierr = MPI_File_open (MPI_COMM_WORLD, cstr, MPI_MODE_RDONLY, MPI_INFO_NULL, &in);
  if (ierr)
    {
      cout << " can't open file " << endl;
    }

  long perpsize;//perprocess size
//    char *chunk;
  //read relevant chunk
  int error = MPI_File_get_size (in, &filesize);
  if (error != MPI_SUCCESS) cout << " cannot get file size " << endl;;
  filesize--;

  perpsize = (filesize) / world_size;

  globalstart = rank * perpsize;
  globalend = globalstart + perpsize - 1;

  if (rank == world_size - 1)
    {
      globalend = filesize - 1;
      globalstart = (rank * perpsize) - overlap;
    }

  if (rank != 0)
    {
      globalstart = (rank * perpsize) - overlap;
    }

  //add overlap to the end
  if (rank != world_size - 1)
    globalend += overlap;

  perpsize = globalend - globalstart + 1;

  long chunk_lo = 1073741824;

  int number_of_chunks = ceil ((perpsize) / chunk_lo) + 1;

  char *chunk = (char *) malloc ((perpsize + 1) * sizeof (char));

  long index = 0;
  long current_chunk = chunk_lo;

  for (int i = 0; i < number_of_chunks; i++)
    {

      if (index >= perpsize)
        break;


//        char *chunk_lo_arr = (char *) malloc((current_chunk) * sizeof(char));
      MPI_Offset globalstart_lo = globalstart + i * current_chunk;
//        cout<<" rank "<<rank<<" start reading  for index"<<index<<" global end "<<perpsize<<"current chunk"<<current_chunk<<endl;
      //read corresponding part
      MPI_File_read_at_all (in, globalstart_lo, &chunk[index], current_chunk, MPI_CHAR, MPI_STATUS_IGNORE);


//        cout<<" rank "<<rank<<" reading completed for index"<<index<<" global end "<<perpsize<<endl;
      index = index + current_chunk;
//        memcpy(&chunk[index], chunk_lo_arr, current_chunk);
      if (index + chunk_lo >= perpsize)
        current_chunk = perpsize - index;


//        cout<<" rank "<<rank<<" trying to free"<<endl;
//        free(chunk_lo_arr);
//        cout<<" rank "<<rank<<" free completed"<<endl;
    }

  MPI_File_close (&in);
  cout << "rank" << rank << " mpi read complete " << perpsize << endl;
  chunk[perpsize] = '\0';
  long locstart = 0, locend = perpsize;
  vector <vector<VALUE_TYPE>> output;


  //move to next full delim of number
  if (rank != world_size - 1)
    {
      while (chunk[locend] != '\n')
        locend--;
      locend++;
    }

  if (rank != 0)
    {
      while (chunk[locstart] != '\n')
        locstart++;
      locstart--;
    }

  perpsize = locend - locstart + 1;
  vector<VALUE_TYPE> v;

  stringstream str (chunk);
  string token;
  cout << "rank:" << rank << ":size:" << str.str ().length () << endl;
  while (getline (str, token))
    {
      std::stringstream linestream (token);
      std::string data;
      VALUE_TYPE n;
      int di = 0;
      while (std::getline (linestream, data, ' '))
        {
          v.push_back (atoi (data.c_str ()));
        }
      if (v.size () == dimension)
        {
          output.push_back (v);
        }
      v.clear ();

    }

  long expected_chunk_size = total_data_set_size / world_size;

  if (rank == world_size - 1)
    expected_chunk_size = total_data_set_size - rank * (total_data_set_size / world_size);

  cout << " rank " << rank << " expected chunk size" << expected_chunk_size << " output size " << output.size ()
       << endl;

  vector <vector<VALUE_TYPE>> final_vec = output;
  if (rank == 0 and output.size () > expected_chunk_size)
    {
      final_vec = slice (output, 0, expected_chunk_size - 1);
    }
  else if (output.size () > expected_chunk_size)
    {
      int total_length = output.size () - expected_chunk_size;
      int first_index = total_length / 2;
      int last_index = output.size () - (total_length - first_index);
      final_vec = slice (output, first_index, last_index - 1);
    }

  for (int i = 0; i < final_vec.size (); i++)
    {
      if (final_vec[i].size () != 960)
        {
          cout << " rank " << rank << " index" << i << " dime" << final_vec[i].size () << endl;
        }
    }

  cout << " rank " << rank << "Output size:" << final_vec.size () << ":" << final_vec[0].size () << endl;

  cout << endl;
  return final_vec;
}

vector <vector<float>>
drpt::ImageReader::mpi_file_read (string path, int rank, int world_size, int overlap,
		long total_data_set_size, int data_type_bytes, int offset, int dimension)
{
	int data_node_byte = dimension *data_type_bytes;

	cout<<"data node bytes"<<data_node_byte<<endl;

  vector <vector<VALUE_TYPE>> final_vec;
  MPI_Offset globalstart, globalend, filesize;
  MPI_File in;
  const char *cstr = path.c_str ();
  int ierr = MPI_File_open (MPI_COMM_WORLD, cstr, MPI_MODE_RDONLY, MPI_INFO_NULL, &in);
  if (ierr)
    {
      cout << " can't open file " << endl;
    }

  long perpsize;//perprocess size
//    char *chunk;
  //read relevant chunk
  int error = MPI_File_get_size (in, &filesize);
  if (error != MPI_SUCCESS) cout << " cannot get file size " << endl;;
//    filesize--;


  filesize = filesize - offset;

  long  global_nodes =      filesize/data_node_byte;
  long file_size_fraction = global_nodes/total_data_set_size;

  cout<<"file size fractions"<<file_size_fraction<<endl;

  filesize = filesize/file_size_fraction;
  long total_data_nodes = filesize / data_node_byte;

  cout << " rank " << rank << " total data size " << total_data_nodes << endl;

  long process_data_nodes = total_data_nodes / world_size;

  if (rank == world_size - 1)
    process_data_nodes = total_data_nodes - rank * process_data_nodes;

  long process_bytes = process_data_nodes * data_node_byte;

  long global_start = rank * process_bytes;



  if (rank == 0)
    global_start = offset;

  long global_end = (rank + 1) * process_bytes - 1;

  if (rank == world_size - 1)
    global_end = filesize - 1;

  char *chunk = (char *) malloc ((process_bytes+1) * sizeof (char));

  long chunk_lo = 1073741824;

  if (chunk_lo >= process_bytes)
    {
      chunk_lo = process_bytes;
    }

  long index = 0;
  long current_chunk = chunk_lo;

  while (index < process_bytes)
    {

      MPI_Offset globalstart_lo = global_start + index;

      MPI_File_read_at_all (in, globalstart_lo, &chunk[index], current_chunk, MPI_CHAR, MPI_STATUS_IGNORE);

      index = index + current_chunk;

      if (index + chunk_lo >= process_bytes)
        current_chunk = process_bytes - index;

    }

  chunk[perpsize] = '\0';

  long count = 0;

  long total_arr_size = (process_bytes) * sizeof (char);

  long co = total_arr_size / (dimension*data_type_bytes);
  long co_in = 0;
  vector <vector<VALUE_TYPE>> output (co);

#pragma omp parallel for
  for (int i = 0; i < co; i++)
    {
      vector<VALUE_TYPE> v (dimension);
      for (int j = 0; j < dimension; j++)
        {
          int inner_index = 0;
          char arr_c[data_type_bytes];
          int start_index = j*data_type_bytes + i * dimension*data_type_bytes;
          int end_index = j*data_type_bytes + i * dimension*data_type_bytes + data_type_bytes;
          float result;
          std::copy(reinterpret_cast<const char*>(&chunk[start_index]),
                    reinterpret_cast<const char*>(&chunk[end_index]),
                    reinterpret_cast<char*>(&result));

          float  x = result;
          if (x > 1e+05)
             x =0;
//          float x = (float) (c);
          v[j] = x;
        }
      output[i] = v;
    }

  return output;
}





