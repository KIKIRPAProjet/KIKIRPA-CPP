# SIFT test images

Place two PNG images named `image_a.png` and `image_b.png` in this folder.
They should show the same scene or have enough shared visual detail for SIFT
to find at least four reliable correspondences in a 30% overlap region. The
test expects the common area at the right edge of `image_a.png` and the left
edge of `image_b.png`.

Run the test with CTest from the repository root:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

The test prints the SIFT keypoint count for each image and the number of
reliable correspondences between them.





pour test :
.\sift_matcher_test.exe ..\..\tests\data\sift\image_a.png ..\..\tests\data\sift\image_b.png