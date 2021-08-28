#include <mpi.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> 
#include <math.h>
#include <time.h>
#define UP    3
#define DOWN  1
#define LEFT  2
#define RIGHT 0

#define KSIDE 3 
#define XWIDTH 256
#define YWIDTH 256
// 256*256 = 65,536
#define MAXVAL 65535


#if ((0x100 & 0xf) == 0x0)
#define I_M_LITTLE_ENDIAN 1
#define swap(mem) (( (mem) & (short int)0xff00) >> 8) +	\
  ( ((mem) & (short int)0x00ff) << 8)
#else
#define I_M_LITTLE_ENDIAN 0
#define swap(mem) (mem)
#endif


// ============================================================================================================================================================
// 1. utilities for managinf pgm files
//
//  * write_pgm_image
//  * read_header
//  * swap_image
//  * read_pixels2
//  
// 2. routine for bluring an image
//
//  * identify_thread
//  * blur
//
// ============================================================================================================================================================
//  WRITE 

void write_pgm_image( void *image, int maxval, int xsize, int ysize, const char *image_name)
/*
 * image        : a pointer to the memory region that contains the image
 * maxval       : either 255 or 65536
 * xsize, ysize : x and y dimensions of the image
 * image_name   : the name of the file to be written
 *
 */
{
  FILE* image_file; 
  image_file = fopen(image_name, "w"); 
  
  // Writing header
  // The header's format is as follows, all in ASCII.
  // "whitespace" is either a blank or a TAB or a CF or a LF
  // - The Magic Number (see below the magic numbers)
  // - the image's width
  // - the height
  // - a white space
  // - the image's height
  // - a whitespace
  // - the maximum color value, which must be between 0 and 65535
  //
  //

  // returns 1 if maxval<=255 and 2 if >=255
  int color_depth = 1 + ( maxval > 255 );

  fprintf(image_file, "P5\n# generated by\n# M. Danese \n%d %d\n%d\n", xsize, ysize, maxval);
  
  // Writing file
  fwrite( image, 1, xsize*ysize*color_depth, image_file);  

  fclose(image_file); 
  return ;

  /* ---------------------------------------------------------------

     TYPE    MAGIC NUM     EXTENSION   COLOR RANGE
           ASCII  BINARY

     PBM   P1     P4       .pbm        [0-1]
     PGM   P2     P5       .pgm        [0-255]
     PPM   P3     P6       .ppm        [0-2^16[
  
  ------------------------------------------------------------------ */
}




// ============================================================================================================================================================


//                               IDENTIFY THREAD PIXELS AND STARTING INDEX
//                                         -    Cart version   -



void identify_thread(int xxth, int yyth, int* xpxl, int* ypxl, int* start_idx, int* start_x, int* start_y, int thpos[2], int thid, int xsize, int ysize)
{
/*
this routine takes and input infos about the decomposed domain and the image under analyisi, and returns the starting point
for the image blurring associated to each thread
*/

    // x axis
    *xpxl = floor(xsize/thpos[0]);     //even division of pixels
    if ((xxth)<xsize%thpos[0]) (*xpxl)++;  // homogeneous addition of extra pixels
    // y axis is analogous
    *ypxl = floor(ysize/thpos[1]);
    if ((yyth)<ysize%thpos[1]) (*ypxl)++;
    
    // ---------------------------------------------

    
    *start_idx = 0;
    *start_x   = 0;
    *start_y   = 0;
    *start_x  = xxth*floor(xsize/thpos[0]);  // this if xsize and xxth are perfectly divisible
    *start_x += (xxth <= xsize%thpos[0])? xxth : xsize%thpos[0];  // add extra starting point if t
    
    *start_y  = yyth*floor(ysize/thpos[1]); 
    *start_y += (yyth <= ysize%thpos[1])? yyth : ysize%thpos[1]; 

    *start_y  *=  xsize;
    *start_idx = *start_x + *start_y;

}




// ============================================================================================================================================================


//                               READ HEADER


void read_header( int *maxval, int *xsize, int *ysize, const char *image_name, FILE** file)
/*
 * image        : a pointer to the pointer that will contain the image
 * maxval       : a pointer to the int that will store the maximum intensity in the image
 * xsize, ysize : pointers to the x and y sizes
 * image_name   : the name of the file to be read
 *
 */
{
  *file = fopen(image_name, "r"); 

  *xsize = *ysize = *maxval = 0;
  
  char    MagicN[2];
  char   *line = NULL;
  size_t  k, n = 0;

    
  /* --------------------------------------------------------------- */


  // get the Magic Number - first element
  k = fscanf(*file, "%2s%*c", MagicN );


    
  /* --------------------------------------------------------------- */


  // skip all the comments
  // getline(&buffer,&size,stdin);
  k = getline( &line, &n, *file);
  while ( (k > 0) && (line[0]=='#') ){
    k = getline( &line, &n, *file);
  }

    
  /* --------------------------------------------------------------- */


  // now that all lines are skipped read x and y dimension and the maxval 
  // remember https://stackoverflow.com/questions/22330969/using-fscanf-vs-fgets-and-sscanf
  if (k > 0)
    {
      k = sscanf(line, "%d%*c%d%*c%d%*c", xsize, ysize, maxval);
      if ( k < 3 )
	if(fscanf(*file, "%d%*c", maxval)!=1){
	  printf("no maxval was provided\n");
	  return;
	}
    }
  // in the case I am givning some bad input
  else
    {
      *maxval = -1;         // this is the signal that there was an I/O error
			    // while reading the image header
      free( line );
      return;
    }
  free( line );

} 


// ============================================================================================================================================================


//                               READ PIXELS


void read_pixels2( void **image, int *maxval, int *xpxl, int *ypxl, const char *image_name, FILE** file, int start_idx, int nths, int thid, int thpos[2], int xyth[2], int ysize, int xsize) 
{   
/*
 This routine makes every thread read the pixel values of interest the image, avoiding reading the image more than once or any communication
*/


  *image = NULL;

  int color_depth = 1 + ( *maxval > 255 );
  unsigned int size =  (*xpxl) * (*ypxl) * color_depth;
  
  if ( (*image = (char*)malloc( size )) == NULL )
    {
      fclose(*file);
      *maxval = -2;       
      *xpxl  = 0;
      *ypxl  = 0;
      printf("memory is full.\n");
      return;
    }

// x and y pixels must be recomputed
  MPI_Barrier(MPI_COMM_WORLD);
  for (int ydim=0; ydim<thpos[1]; ydim++ )
    {
    int ymax = floor(ysize/thpos[1]);
    if ((ydim)<(ysize%thpos[1])) {ymax++;}
    for(int ypxlidx=0; ypxlidx<ymax; ypxlidx++)
      {
      for(int xdim=0; xdim<thpos[0]; xdim++)
        {
        int xmax = floor(xsize/thpos[0]);     //even division of pixels
        if ((xdim)<(xsize%thpos[0])){ xmax++;}  // homogeneous addition of extra pixels
        void *line; 
        line  = NULL;
        line  = (char*)malloc( (xmax)*color_depth );

          if(fread( line,  1, (xmax)*color_depth, *file)==(xmax)*color_depth){
	  if ( (xyth[0]==xdim) && (xyth[1]==ydim))
	    {
	    for (int k=0; k< color_depth*xmax; k++){
              ((char*)*image)[k+ypxlidx*(xmax)*color_depth] = ((char*)line)[k];
            }
	  }
	  } else {
	    printf("wrong size\n");
	    return;
	  }
          MPI_Barrier(MPI_COMM_WORLD);
	}
      }
    }



  fclose(*file);
  return;
}





// ============================================================================================================================================================


//                               SWAP PGM



void swap_image( void *image, int xsize, int ysize, int maxval )
/*
 * This routine swaps the endianism of the memory area pointed
 * to by ptr, by blocks of 2 bytes
 *
 */
{
  if ( maxval > 255 )
    {
      // pgm files has the short int written in
      // big endian;
      // here we swap the content of the image from
      // one to another
      //
      unsigned int size = xsize * ysize;
      for ( int i = 0; i < size; i++ )
  	((unsigned short int*)image)[i] = swap(((unsigned short int*)image)[i]);
    }
  return;
}



// ============================================================================================================================================================


//                               BLUR PGM


void * blur( void *image, int xsize, int ysize, int start_idx, int start_x, int start_y, int xxth, int yyth, int xpxl, int ypxl, int maxval, int ksize, float kernel[ksize][ksize], float knorm, int khalfsize, short unsigned int* halo[4])
/*
  This routine takes as input the image to blur, its x and y size, its maxval (as above), 
  the kernel size (ksize), the kernel matrix valuse and its normalisation  
 */
{
  short int *sImage;   // the image when a two bytes are used for each pixel
  void      *tempptr;
 
  start_y /= xsize; // instead of repeating this division at every loop

  
  /* --------------------------------------------------------------- */
    //   2 bytes
  
      sImage = (unsigned short int*)calloc( xpxl*ypxl, sizeof(short int) );
      unsigned short int _maxval = swap((unsigned short int)maxval);
      for ( int yy = 0; yy < ypxl; yy++ ){
        for( int xx = 0; xx < xpxl; xx++ ){
          int idx =  yy*xpxl + xx; 
	  double xxyy = 0;
	  for (int yks=-khalfsize; yks<khalfsize+1; yks++){
	    for (int xks=-khalfsize; xks<khalfsize+1; xks++){
              // check global borders (x_start and y_start for real values)
	      if (start_x + xx + xks < xsize && start_y + yy + yks < ysize  && start_x+xx+xks >= 0 && start_y+yy+yks >=0){
	        //check local borders
		if (xx+xks<xpxl && yy+yks<ypxl && xx+xks>=0 && yy+yks>=0){
	          int sidx = idx + yks*xpxl+ xks; 
	          xxyy += kernel[khalfsize+yks][khalfsize+xks]*((unsigned short int*)image)[sidx];
	        } // RIGHT
		  else if (xx+xks>=xpxl){  
                    xxyy += kernel[khalfsize+yks][khalfsize+xks]*((unsigned short int*)halo[RIGHT])[(xx+xks-xpxl) + (yy+yks+khalfsize)*khalfsize];
		  } // LEFT
		  else if (xx+xks<0){ 
	            xxyy += kernel[khalfsize+yks][khalfsize+xks]*((unsigned short int*)halo[LEFT])[(xx+xks+khalfsize) + (yy+yks+khalfsize)*khalfsize];
                  } // UP
		  else if (yy+yks<0){ 
	            xxyy += kernel[khalfsize+yks][khalfsize+xks]*((unsigned short int*)halo[UP])[(xx+xks) + (khalfsize+yy+yks)*xpxl];
                  } // DOWN
		  else if (yy+yks>=ypxl){ 
	            xxyy += kernel[khalfsize+yks][khalfsize+xks]*((unsigned short int*)halo[DOWN])[(xx+xks) + (yy+yks-ypxl)*xpxl];
                  }

	      }
	    }
	  }
	  sImage[yy*xpxl+xx] = round(xxyy/knorm);
	  idx++;
        }
      }
      tempptr = (void*)sImage;	

  return tempptr;
}



// ============================================================================================================================================================


//                               MAIN


// ============================================================================================================================================================


int main( int argc, char *argv[ ] ) 
{ 
  int xsize;
  int ysize;
  int maxval;
  struct timespec ts;
  double startt, stopt;
  int nths,thid;
  int ksize      = 25;
  int ktype      = 0;
  float kfactor  = 0.2;
  char *input_image_name;
  char *output_image_name;

  MPI_Status status; 
  MPI_Request request;
  const int master = 0;
  int tag = 123;
  

  MPI_Init(&argc,&argv);
  MPI_Comm_size(MPI_COMM_WORLD,&nths);  
  startt = MPI_Wtime();
  // decompose in a 2D cartesian grid
  int thpos[2] = {0, 0};
  MPI_Dims_create(nths, 2, thpos);

  //set no periodicity
  int periods[2] = {0,0};

  // MPI assignes arbitrary ranks
  int reorder=1;
  
  // Create a communicator given the 2D torus topology.
  MPI_Comm grid_communicator;
  MPI_Cart_create(MPI_COMM_WORLD, 2, thpos, periods, reorder, &grid_communicator);

  //my thread id in the new communicator
  MPI_Comm_rank(grid_communicator, &thid);

  //get coordinate in the new communicator
  int    xyth[2];
  MPI_Cart_coords(grid_communicator, thid, 2, xyth);




    // read input parameters
    int arg_num=1;
    if ( argc > arg_num ) {
     ktype   = atoi( argv[arg_num] );
     arg_num++;
     if (ktype>2 || ktype <0){
       printf("Invalid ktype\n");
       return 0;
       }
    if ( argc > arg_num ) {
     ksize   = atoi( argv[arg_num] );
     arg_num++;
    if (ktype==1) {if ( argc > arg_num) {
                     kfactor = atof( argv[arg_num]);
                     arg_num++;
                     if (kfactor>1 || kfactor<0){
                       printf("Invalid kfactor\n");
                       return 0;
                     }
                   }
    }
    if ( argc > arg_num ) {
     input_image_name  = malloc(strlen( argv[arg_num])+1);
     strcpy(input_image_name, argv[arg_num]);
     arg_num++;
    if ( argc > arg_num ) {
     output_image_name  = malloc(strlen( argv[arg_num])+1);
     strcpy(output_image_name, argv[arg_num]);
     arg_num++;
     } } } } 
    
    //if no output image name is provided
    if(arg_num<(5+ktype%2)) {
     output_image_name  = malloc(15);
     strcpy(output_image_name, "mpi_output.pgm");
    }
    if(arg_num<(4+ktype%2)) {
     input_image_name   = malloc(16);
     strcpy(input_image_name, "../check_me.pgm");
    }


  void *ptr; 
  int skip_counter=0;
  FILE *file;

   /*  ------------------------------------------------------- 
  
           THREADS SET UP     
  
       -------------------------------------------------------

       The original image is divided into a grid of subimages.

         * nths        - total number of threads = number of 
	                 subimages to be considered
	 * thpos[0]    - number of division along the x axis
	 * thpos[1]    - number of division along the y axis
	 * xpxl        - number of pixel in the x axis that 
	                 each column of subimages should have
	 * ypxl        - number of pixel in the y axis that 
	                 each row of subimages should have
         * thid        - number of the current thread
         * xyth[0]     - x coordinate of the sub image (see below)
         * xyth[1]     - y coordinate of the sub image (see below)


          illustration with 9 threads (nths=9):
          best division: 3x3 grid (i.e. thpos[0]=3 and thpos[1]=3)

	       _________________________          original image       
          ^   |  (0,0) '  (0,1) '  (0,2) |        divided in 9 subimages          
          |   |      0 '      1 '      2 |        following a 3x3 grid
          |   |--------'--------'--------|       / 
    ysize |   |  (1,0) '  (1,1) '  (1,2) |   <--ˊ
          |   |      3 '      4 '      5 |            where the subimage is:
          |   |--------'--------'--------|            ---------------------  ^
          |   |  (2,0) '  (2,1) '  (2,2) |           '                    '  |
	  ˅   | _____6_'______7_'______8_|           '  (xyth[0],xxth[1]) '  |
                                                     '            thid    '  |  ypxl
	      <-------------------------->	     '                    '  |
                        xsize                        '--------------------'  ˅
						                                     
						              xpxl 
						      <------------------->            

       ------------------------------------------------------- */
  
  read_header( &maxval, &xsize, &ysize, input_image_name, &file);
  int xpxl, ypxl;
  int start_idx, start_x, start_y;

  identify_thread(xyth[0], xyth[1], &xpxl, &ypxl, &start_idx, &start_x, &start_y, thpos, thid, xsize, ysize);
  
  read_pixels2( &ptr, &maxval, &xpxl, &ypxl, input_image_name, &file, start_idx, nths, thid, thpos, xyth, ysize, xsize);


  // the sharing of pixel dimensions of subimages and their starting points is useful now
  // and necessary in the final stage.
  void *xpxlrcounts, *ypxlrcounts, *startidxrcounts;
  xpxlrcounts   = malloc(nths * sizeof(int));
  ypxlrcounts   = malloc(nths * sizeof(int));
  startidxrcounts = malloc(nths * sizeof(int));

  MPI_Allgather(&xpxl,      1, MPI_INT, xpxlrcounts,     1, MPI_INT, grid_communicator);;
  MPI_Allgather(&ypxl,      1, MPI_INT, ypxlrcounts,     1, MPI_INT, grid_communicator);
  MPI_Allgather(&start_idx, 1, MPI_INT, startidxrcounts, 1, MPI_INT, grid_communicator);
  
  if ( I_M_LITTLE_ENDIAN )
    swap_image( ptr, xpxl, ypxl, maxval);


   /*  ------------------------------------------------------- 
  
           KERNEL SET UP   
  
       ------------------------------------------------------- */

    float kernel[ksize][ksize];
    float knorm = 0;
    int khalfsize   = (ksize-1)/2; // radius of the kernel


    // ---------------------------------------------
    // average kernel
    if (ktype==0) {
      for (int i=0; i<ksize;i++){
        for (int j=0; j<ksize;j++){
          kernel[i][j]=1;
	  knorm += kernel[i][j];
        }
      }
    }
    else if (ktype==1) {
    // ---------------------------------------------
    // weight kernel
      for (int i=0; i<ksize;i++){
        for (int j=0; j<ksize;j++){
          kernel[i][j]=1-kfactor;
        }
      }
      knorm = (ksize*ksize-1);
      kernel[khalfsize][khalfsize]=kfactor*(ksize*ksize-1);
    }
    else if (ktype==2) {
    
    // ---------------------------------------------
    // gaussian kernel
      float kden  = 1./(2.*khalfsize*khalfsize);
      knorm = 0;
      for (int i=0; i<ksize;i++){
	float ky=i-khalfsize;
        for (int j=0; j<ksize;j++){
	  float kx=j-khalfsize;
          kernel[i][j]=expf( -((kx*kx)+(ky*ky))*kden );
	  knorm += kernel[i][j];
	}
      }
    }

 /*  ------------------------------------------------------- 

         HALO LAYERS   

     ------------------------------------------------------- */

  // ---------------------------------------------
  
  unsigned short int *halo[4];
  halo[UP]    = (unsigned short int*)calloc( (xpxl)*khalfsize, sizeof(short int) );
  halo[DOWN]  = (unsigned short int*)calloc( (xpxl)*khalfsize, sizeof(short int) );
  halo[RIGHT] = (unsigned short int*)calloc( (ypxl+2*khalfsize)*khalfsize, sizeof(short int) );
  halo[LEFT]  = (unsigned short int*)calloc( (ypxl+2*khalfsize)*khalfsize, sizeof(short int) );
  
  int datatype_count;   
  int datatype_block; 
  int datatype_stride; 
  int nn_source, nn_dest;
  // 
  // vertical and horizontal
  for(int disp=-1; disp<=1; disp+=2){
    for(int dir=0; dir<2; dir++){
      MPI_Cart_shift(grid_communicator, dir, disp, &nn_source, &nn_dest);
      //set sending datatype
      datatype_count  = ypxl;
      datatype_block  = khalfsize;
      datatype_stride = xpxl;
      //since all has same size, should work 
      MPI_Datatype type, finaltype;     
      MPI_Type_vector( datatype_count, datatype_block, datatype_stride , MPI_UNSIGNED_SHORT, &type);
      MPI_Type_commit(&type);
      MPI_Aint unshortlb, unshortsize, lb = 0;
      MPI_Type_get_extent(MPI_UNSIGNED_SHORT, &unshortlb, &unshortsize);   
      MPI_Type_create_resized(type, lb, unshortsize, &finaltype);
      MPI_Type_commit(&finaltype);

      MPI_Request req;
      MPI_Status status;
      if(nn_source != MPI_PROC_NULL) {
        // UP
	if (dir==1 && disp==1){
	  MPI_Recv(&(((unsigned short int*)halo[UP])[0]),xpxl*khalfsize,MPI_UNSIGNED_SHORT,nn_source,123,grid_communicator,&status);
	} // DOWN
	if (dir==1 && disp==-1){
	  MPI_Recv(&(((unsigned short int*)halo[DOWN])[0]),xpxl*khalfsize,MPI_UNSIGNED_SHORT,nn_source,123,grid_communicator,&status);
	} // RIGHT
	if (dir==0 && disp==-1){
	  MPI_Recv(&(((unsigned short int*)halo[RIGHT])[khalfsize*khalfsize]),khalfsize*ypxl,MPI_UNSIGNED_SHORT,nn_source,789,grid_communicator,&status);
	} // LEFT
	if (dir==0 && disp==1){
	  MPI_Recv(&(((unsigned short int*)halo[LEFT])[khalfsize*khalfsize]),khalfsize*ypxl,MPI_UNSIGNED_SHORT,nn_source,789,grid_communicator,&status);
	}
      }
      if(nn_dest != MPI_PROC_NULL) {
        // UP
        if (dir==1 && disp==1){
	  MPI_Send(&(((unsigned short int*)ptr)[xpxl*(ypxl - khalfsize)]),xpxl*khalfsize,MPI_UNSIGNED_SHORT,nn_dest,123,grid_communicator );
        } // DOWN
        if (dir==1 && disp==-1){
	  MPI_Send(&(((unsigned short int*)ptr)[0]),xpxl*khalfsize,MPI_UNSIGNED_SHORT,nn_dest,123,grid_communicator );
        } // RIGHT
        if (dir==0 && disp==-1){
	  MPI_Send(&(((unsigned short int*)ptr)[0]),1,finaltype,nn_dest,789,grid_communicator );
        } // LEFT
        if (dir==0 && disp==1){
	  MPI_Send(&(((unsigned short int*)ptr)[xpxl-khalfsize]),1,finaltype,nn_dest,789,grid_communicator );
        } 
      }
      
    } // end for dir
  } //end for disp

  // ---------------------------------------------
  // corners
  for (int deltax=1; deltax>=-1; deltax-=2){
    for (int deltay=1; deltay>=-1; deltay-=2){
      nn_source = MPI_PROC_NULL;
      nn_dest   = MPI_PROC_NULL;
      int nn_source_coord[2] = {xyth[0]-deltax, xyth[1]-deltay};
      int nn_dest_coord[2]   = {xyth[0]+deltax, xyth[1]+deltay};
      
      if(nn_dest_coord[0]>=0   && nn_dest_coord[1]>=0   && nn_dest_coord[0]<thpos[0]   && nn_dest_coord[1]<thpos[1]  ) 
        MPI_Cart_rank(grid_communicator, nn_dest_coord,   &nn_dest);
      if(nn_source_coord[0]>=0 && nn_source_coord[1]>=0 && nn_source_coord[0]<thpos[0] && nn_source_coord[1]<thpos[1] )
        MPI_Cart_rank(grid_communicator, nn_source_coord, &nn_source);
    
      datatype_count  = khalfsize;
      datatype_block  = khalfsize;
      datatype_stride = xpxl;
      
      MPI_Datatype type, finaltype;     
      MPI_Type_vector( datatype_count, datatype_block, datatype_stride , MPI_UNSIGNED_SHORT, &type);
      MPI_Type_commit(&type);
      MPI_Aint unshortlb, unshortsize, lb = 0;
      MPI_Type_get_extent(MPI_UNSIGNED_SHORT, &unshortlb, &unshortsize);   
      MPI_Type_create_resized(type, lb, unshortsize, &finaltype);
      MPI_Type_commit(&finaltype);
    
      MPI_Request req;
      MPI_Status status;
      if (nn_source != MPI_PROC_NULL)
        {  // 0
        if (deltax==-1 && deltay==1){
          MPI_Recv(&(((unsigned short int*)halo[RIGHT])[0]),khalfsize*khalfsize,MPI_UNSIGNED_SHORT,nn_source,789,grid_communicator,&status);
        }  // 1
        if (deltax==-1 && deltay==-1){
          MPI_Recv(&(((unsigned short int*)halo[RIGHT])[(ypxl+khalfsize)*khalfsize]),khalfsize*khalfsize,MPI_UNSIGNED_SHORT,nn_source,789,grid_communicator,&status);
        }  // 2
        if (deltax==1 && deltay==-1){
          MPI_Recv(&(((unsigned short int*)halo[LEFT])[(ypxl+khalfsize)*khalfsize]),khalfsize*khalfsize,MPI_UNSIGNED_SHORT,nn_source,789,grid_communicator,&status);
        }  // 3
        if (deltax==1 && deltay==1){
          MPI_Recv(&(((unsigned short int*)halo[LEFT])[0]),khalfsize*khalfsize,MPI_UNSIGNED_SHORT,nn_source,789,grid_communicator,&status);
        }
      }
      
      if  (nn_dest != MPI_PROC_NULL)
        {  // 0
        if (deltax==-1 && deltay==1){
	  MPI_Send(&(((unsigned short int*)ptr)[xpxl*(ypxl -khalfsize)]),1,finaltype,nn_dest,789,grid_communicator );
        }  // 1
        if (deltax==-1 && deltay==-1){
	  MPI_Send(&(((unsigned short int*)ptr)[0]),1,finaltype,nn_dest,789,grid_communicator );
        }  // 2
        if (deltax==1 && deltay==-1){
	  MPI_Send(&(((unsigned short int*)ptr)[xpxl-khalfsize]),1,finaltype,nn_dest,789,grid_communicator );
        }  // 3
        if (deltax==1 && deltay==1){
	  MPI_Send(&(((unsigned short int*)ptr)[xpxl*(ypxl -khalfsize)+xpxl-khalfsize]),1,finaltype,nn_dest,789,grid_communicator );
        }
      }
    
    }//end for
  }//end for
  
  // ---------------------------------------------
  

  void *rptr;
  rptr = (void*)ptr;



  rptr = blur( ptr, xsize, ysize, start_idx, start_x, start_y, xyth[0], xyth[1], xpxl, ypxl, maxval, ksize, kernel, knorm, khalfsize, halo);
  //rptr = ptr;




  // ---------------------------------------------
  // identify cases

  /*
  Create a different communicator for every size of subimage.
  *cases - compares of thread i-th pixels vs thread 0 pixels:

    i-th  x   y
   ________________
     0    =   =    no uneven divisions
     1    <   =    uneven division in x    ͞| these two cannot co-exist without 3the 3-rd case being also present
     2    =   <    uneven division in y    _|
     3    <   <    uneven division in both
  
  *cases_counter[cases]          - counts the number of elements
                                   corresponding to each case
  *cases_long_array[cases][nths] - temporarely stores the thid accor- 
                                   ding to the corresponding case
  *cases_thid[cases]             - array of pointers. Same as 
                                   cases_long_array, but with right 
				   dimensions.
  */

  int cases=1;
  if ((((int *)xpxlrcounts)[0] > ((int *)xpxlrcounts)[nths-1]) && (((int *)ypxlrcounts)[0] > ((int *)ypxlrcounts)[nths-1])) { cases = 4;}
  else { if ((((int *)xpxlrcounts)[0] > ((int *)xpxlrcounts)[nths-1]) || (((int *)ypxlrcounts)[0] > ((int *)ypxlrcounts)[nths-1])) { cases = 2;} }
  
  int cases_counter[cases];
  int cases_long_array[cases][nths];
  for (int c=0;c<cases;c++){
    cases_counter[c]=1;
    cases_long_array[c][0]=0;//to take also master always
  }

  for(int i=1;i<nths;i++){//master already taken
    // case both pixels dimensions are smaller then master's
    if ((((int *)xpxlrcounts)[0] > ((int *)xpxlrcounts)[i]) && (((int *)ypxlrcounts)[0] > ((int *)ypxlrcounts)[i])) {
														      cases_long_array[3][cases_counter[3]]=i;
														      cases_counter[3]++;
														      }
         // case one of the two is smaller than master's and the other will never be (else)
    else {if (  ((((int *)xpxlrcounts)[0] > ((int *)xpxlrcounts)[i]) || (((int *)ypxlrcounts)[0] > ((int *)ypxlrcounts)[i])) && (cases==2) ) {
          cases_long_array[1][cases_counter[1]]=i;
	  cases_counter[1]++;

	         // case only x is smaller than master, but also y can be (else wrt to cases==2)
          } else {if (((int *)xpxlrcounts)[0] > ((int *)xpxlrcounts)[i]) {
									   cases_long_array[1][cases_counter[1]]=i;
	                                                                   cases_counter[1]++;
									   }
	         // case only y is smaller than master, but also x can be (else wrt to cases==2)
                 if (((int *)ypxlrcounts)[0] > ((int *)ypxlrcounts)[i]) {
									   cases_long_array[2][cases_counter[2]]=i;
		                                                           cases_counter[2]++;
									   }
          
	         // case none of them is smaller (does not matter if can be)
                 if ((((int *)xpxlrcounts)[0] == ((int *)xpxlrcounts)[i]) && (((int *)ypxlrcounts)[0] == ((int *)ypxlrcounts)[i])) {
									   cases_long_array[0][cases_counter[0]]=i;
		                                                           cases_counter[0]++;
									   }
          }
    }
  } //end for

  int *cases_thid[cases];
  for(int c=0;c<cases;c++){
    cases_thid[c]    = (int*)calloc(cases_counter[c],sizeof(int) );
    for(int i=0; i<cases_counter[c];i++){
      cases_thid[c][i] = cases_long_array[c][i];
    }
  }

  
  // ---------------------------------------------
  // create communicator
  //
  /*  *mpi_group[cases] - array of MPI_groups corresponding to 
                          previous cases
      *mpi_group_communicator[cases] - communicators correspon-
                           ding to the mpi_group.

  */


  MPI_Group world_group;
  MPI_Comm_group(MPI_COMM_WORLD, &world_group);

  // Construct a group containing all of the prime ranks in world_group
  MPI_Group mpi_group[cases];
  MPI_Comm mpi_group_communicator[cases];
  for(int c=0; c<cases;c++){
    MPI_Group_incl(world_group, cases_counter[c], cases_thid[c], &mpi_group[c] );
    MPI_Comm_create_group(MPI_COMM_WORLD, mpi_group[c], c, &mpi_group_communicator[c]  );
  }

  MPI_Group_free(&world_group);
  
  short int *final_pointer;   // the image when a two bytes are used for each pixel
  final_pointer = (unsigned short int*)calloc( xsize*ysize, sizeof(short int) );
  


  // ---------------------------------------------
  //        Create datatype and send it
  //
  /* *finaltype  - created and rewritten for each com-
                   municators. Is used by the master for 
		   reading the incoming values shaping 
		   them already as subimages of the 
		   final image.

     * Gatherv is used for communications, since allows
                   (i)  collection by the master from the 
		       whole communicator
		  (ii) customisation of the receiving datatype,
		       displacement and position.

  */

  // for every communicatore
  for(int g=cases-1; g>=0;g--){
    // create a MPI_Datatype corresponding to a row of a sub image 
    MPI_Datatype type, finaltype;      //group g, element 1 (I non master)
    MPI_Type_vector( ((int *)ypxlrcounts)[cases_thid[g][1]], ((int *)xpxlrcounts)[cases_thid[g][1]] ,  xsize, MPI_UNSIGNED_SHORT, &type);
    MPI_Type_commit(&type);
    MPI_Aint unshortlb, unshortsize, lb = 0;
    MPI_Type_get_extent(MPI_UNSIGNED_SHORT, &unshortlb, &unshortsize);   
    MPI_Type_create_resized(type, lb, unshortsize, &finaltype);
    MPI_Type_commit(&finaltype);
  
    
  
    //displs  -> starting index for all the threads
    //rcounts -> array containing number of elements to be received from each process
    void *rcounts, *displs;
    rcounts = malloc(cases_counter[g] * sizeof(int));
    displs  = calloc(cases_counter[g], sizeof(int)); // initialises to 0
  
    for (int i=0; i<cases_counter[g]; i++){
      // master receives a single row at every iteration
      ((int *)rcounts)[i]  = 1; 
      // at the beginning the masters receives the first row positione at the start of the subimage
      ((int *)displs )[i]  = ((int *)startidxrcounts)[cases_thid[g][i]] ;  
    }
  
  
    if (mpi_group_communicator[g] != MPI_COMM_NULL){
      MPI_Gatherv( rptr,((int *)xpxlrcounts)[cases_thid[g][1]]*((int *)ypxlrcounts)[cases_thid[g][1]] , MPI_UNSIGNED_SHORT, final_pointer, rcounts, displs, finaltype, master, mpi_group_communicator[g]); 
      MPI_Group_free(&mpi_group[g]);
      MPI_Comm_free(&mpi_group_communicator[g]);
  }
  }

   /*  ------------------------------------------------------- 
  
           SAVE AND FINISH
  
       ------------------------------------------------------- */
  
  // reverse and save image
  if(thid == master){
   if ( I_M_LITTLE_ENDIAN )
     swap_image( final_pointer, xsize, ysize, maxval);
     write_pgm_image( final_pointer, maxval, xsize, ysize, output_image_name);
  }

  stopt = MPI_Wtime();
  if (thid==master) printf("time: %f\n", stopt-startt);
  free(ptr);
  free(rptr);
  free(final_pointer);
  free(xpxlrcounts);
  free(ypxlrcounts);
  free(startidxrcounts);
  MPI_Finalize();
} 


