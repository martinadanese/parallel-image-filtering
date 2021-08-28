# parallel-image-filtering
image-blurring algorithm implementing both an OpenMP- and an MPI-based parallel code



## OpenMP code
When running on `nths` threads, the OpenMP code divides the overall work on the input image into `nths` sub-images of work according to a bi-dimensional grid, and assigns to each thread the blurring of one of these sub-images.
The grid is obtained maximising at the same time the number of divisions in `x` (`nthsx`) and `y` direction (`nthsy`), in order to keep the splits on one axis as close as possible to the other.
Each thread (`thid`) is then associated to a grid position (`xxth`, `yyth`) according to:

```
yyth[thid] 🠆 floor(thid/nthsx)
xxth[thid] 🠆 thid % nthsx
```

When possible, the number of pixels of the original image in `x` (or `y`) direction, namely `xsize` (`ysize`), is evenly divided among the `nthsx` columns (`nthsy` rows), leading threads to have same amounts of pixels `xpxl[thid]` (`ypxl[thid]`).
When the number of threads does not allow for a perfect division of the number of pixels of the original image, the first `xsize\% nthsx` (`ysize\% nthsy`) sub-images are allocated with an extra pixel along that axis, namely adding 1 to `xpxl[thid]` (`ypxl[thid]`) as shown in Figure.


![Alt text](division_dark-01.png?raw=true)

Thus, the thread numbered `thid` is mapped the corresponding starting index (`start\_idx`) of the original image as:

<img src="https://render.githubusercontent.com/render/math?math=\texttt{start\_idx[thid]} \longrightarrow \sum_{i=0}^{thid} \texttt{xpxl[i]} + \sum_{i=0}^{thid} \texttt{ypxl[i] * xsize}">

For the blurring, a parallel region is opened setting a close thread affinity policy. Every thread loops over all the pixels of its sub-image, iterating first in `y` and then `x` direction with two `for` loops. Inside, the blurring value of the single pixel is computed. 
This is achieved with two other nested loops running from `-khalfsize` to `+khalfsize` (the kernel integer half-size), and multiplying the pixel and its surrounding elements with the corresponding element of the kernel.
Remaining inside the `y` and `x` loops, these results are collected and summed to find the final value of the blurred pixel.

Note that in the current version of the program the parallel region is opened merely for the blurring, and all the parameters of the sub-images are created beforehand and stored in shared arrays. 
Note that no improvement of performances was obtained with more complex versions, such as 
(i) opening the parallel region at the very beginning of the code and privately defining sub-images parameters herein, and/or 
(ii) writing threads results in a parallel region, always taking care properly of the synchronisation by means of single, critical or atomic regions in order to avoid thread races.

## MPI code

The idea behind MPI implementation is the same discussed for the OpenMP code.
First, the formation of a bi-dimensional Cartesian splitting relies on MPI domain decomposition routines, which allowed also for the creation of a communicator, named `grid\_communicator`.

Then, `xpxl` and `ypxl` values are assigned as seen previously, also for cases in which threads do not allow for an exact division of `xsize` or `ysize`.
The image is opened and read once, letting each segment to be read exactly by the processor that will blur it. 
This approach presents the advantage of opening and reading the image just once, and avoiding communications among threads. 

At this point, halo layers are sent among processors with the blocking functions Send and Recv. Even though halo layers are not modified, blocking functions were chosen because 
(i) non-blocking functions were observed to wrongly exchange data, and 
(ii) for small messages such as halo layers are, system buffers are used, allowing these small messages to perform as non-blocking.
Vertical and horizontal nearest neighbours are directly identified with MPI_Cart_shift, while the diagonal next nearest neighbours are retrieved with MPI_Cart_rank function. 
Because the opening of numerous channels one after the other would lead to high latency, new MPI_Datatypes were used for sending the required data as a unique block. 
This is easily achieved assigning (i) the amount of pixels that must be exchanged in each row of the sending thread as the block value, 
(ii) `xpxl` of the sending thread as stride and (iii) the amount of lines that must be exchanged as counts.


Finally each process computes the blurring analogously to the previous case, but checking whether it is required to use any halo layer on the borders.
Resulting data are gathered with MPI_Gatherv function and again creating MPI datatype so that the master processor can directly store then into the correct position.
Moreover, in case threads present different amounts of `xpxl` and/or `ypxl`, separate MPI communicators are created isolating threads with different dimensions 
(e.g. for the case presented in the Figure above, four Datatypes are required). 
This allows the master to correctly gather the data, and to avoid a line-by-line communication which would have required repeated openings and consequential increase in latency.

## Scalability

Weak and a strong scalability tests were conducted with two kernel sizes, `ksize=11` and `101` both for MPI and OpenMP codes.
For `ksize=11` results rationalised in terms of speed-up report a detachment from the ideal trend with both for OpenMP and MPI code
the latter having a more severe deterioration, but still remaining above the 50\% of the ideal speed-up. 
This discrepancy is due to the extra communications that MPI requires.

On the contrary, `ksize=101` results in almost perfect trends for both the codes.
For the weak scalability, each processor was assigned to an equal amount of work, namely the blurring of a `4000 x 4000` pixels image created on-the-fly while running the scalability script. 
Results appear in agreement with those seen for the strong scalability, being the trends for `ksize=101` indiscernible from the ideals, while those of `ksize=11` worse, but still acceptable.



![Alt text](results/strongscalability.png?raw=true)
![Alt text](results/weakscalability.png?raw=true)











