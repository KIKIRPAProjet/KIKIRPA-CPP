# Automatic SIFT tile positions

Automatic mode reads image filenames containing coordinates in the form
`_r00_c00` (row and column). It compares only direct neighbors: the tile to the
right and the tile below. Diagonal pairs are skipped. Images without grid
coordinates, such as `image_a.png` and `image_b.png`, are ignored.

Run from the repository root in PowerShell:

```powershell
.\build\Debug\sift_matcher_test.exe .\tests\data\sift 0.3
```

The program prints a C++ `std::vector<stitching::TilePositionData>` initializer.
The struct is declared in `include/core/Types.h` and contains `tile_id` and the
top-left `cv::Point2d position`. The first tile in the connected grid is the
reference at `(0, 0)`. Positions are estimated by least squares from all
successful neighbor offsets; disconnected tiles are not included.

The offset constraint for each matched pair is `positionB - positionA =
median(pointA - pointB)`. The optional overlap argument is a fraction from 0 to
1 and defaults to 0.3.

Manual pair mode remains available:

```powershell
.\build\Debug\sift_matcher_test.exe .\tests\data\sift\NOM_r00_c01.png .\tests\data\sift\NOM_r01_c01.png vertical
```
