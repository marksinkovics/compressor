# Video: notes for a later run (2026-11-21)

Run 8, item 5. Nothing here is built. This page is what the image track (run 8) and the GPU spike (run 7) say about a video coder, and a plan with kill criteria for a run that would try one.

## What carries over from the image work

- **Intra frames:** the lossy experiment (`encode_lossy_image`, an 8×8 DCT with coefficients coded by a context mixer) comes within 0.6–2.6% of JPEG XL on photos at equal SSIMULACRA2, and 10% below WebP and AVIF's still images (`docs/benchmarks.md`, "Image track: lossy experiment"). As the key frames of a video it would be competitive with the intra coding of current video codecs.
- **Tiles and passes:** 512-pixel tiles cost 0.4–0.7% and let a decoder skip what it doesn't show and use one thread per tile. That is how HEVC and AV1 tiles are used, and frequency passes are a form of scalable coding.
- **Screen content:** the lossless coder's whole-pixel copies and 2-D match model took screens from 0.426 to 0.113 bpp, level with WebP lossless. Screen recordings are mostly unchanged pixels and repeated glyphs, which is what intra block copy and palette modes do in HEVC-SCC and AV1. A screen-recording mode could be the lossless coder with the previous frame as one more copy source.
- **Deblocking:** today it runs on the decoded image. In video it has to run inside the loop, on the frames used as references, so encoder and decoder predict from the same pixels.

## What video needs beyond it

- **Inter prediction:** motion-compensated prediction of each block from one or two reference frames (block sizes 8 to 64, quarter-pixel interpolation), with motion vectors predicted from the neighbours. Residuals of good predictions are mostly zero, so the coefficient model would need contexts for "skip" and for the prediction's quality.
- **Frame types and order:** I frames (intra only, where a segment can start), P frames (one reference), and B frames (two references, coded out of display order). A group of pictures is typically 1–2 seconds.
- **Rate control:** a target bit rate per segment, with the quantiser chosen per frame and per block. The image experiments' quality scale is the start; per-block adaptation by variance didn't help SSIMULACRA2 on stills, so a video version should be driven by the metric or by temporal masking.
- **Decoding speed:** this decides the entropy coder. The lossy image decoder runs at about 0.18 s per 4-megapixel image on one thread, so a 1080p frame (2 megapixels) takes about 0.09 s, about 11 frames per second on one thread and about 60 with tiles on 6. The lossless coder (about 1.5 µs per pixel) is 100× too slow for real time. Context mixing is fine for intra frames, but most of a video's bits and decode time are in inter frames, where an adaptive coder with fixed contexts and no mixing (as in AV1) or tANS with per-frame tables (coder 8) is the realistic choice.

## Motion search on the GPU

Run 7 found the GPU worth it only where the work is wide and independent: LZT match lists made level 7 2.05× faster, while serial coders lost (CM reached 20 MB/s at best with 1024 blocks in flight, `docs/benchmarks.md`, "GPU spike: CM over many blocks"). Motion search is the wide kind. Each block's cost over its candidate vectors is a sum of absolute differences, independent of every other block. A full search of ±32 pixels for 8×8 blocks of a 1080p frame is about 2 megapixels × 4,225 candidates × 64 differences per 64 pixels, about 8.8 × 10⁹ operations. That takes a few milliseconds on the M3 Max's GPU against seconds on one CPU core. A hierarchical search (a coarse search on downscaled frames, refined around the best vector) cuts it by about 50×, and then the CPU can keep up for offline encoding. The Metal plumbing exists (`gpu/`, `COMPRESSOR_METAL`); the kernel and a CPU-vs-GPU benchmark would be the first step.

## Segments for HLS and DASH

- Each segment (typically 2–6 seconds) starts with an I frame that refers to nothing earlier, so a player can start at any segment and switch between bit rates at segment boundaries. Several renditions at different bit rates and resolutions are encoded with aligned segment boundaries.
- HLS and DASH carry segments as fragmented MP4 (CMAF) or MPEG-TS, with the codec named in the playlist. Browsers and players decode only registered codecs (H.264, HEVC, VP9, AV1) in hardware or software. A codec of our own plays only through our decoder: compiled to WebAssembly (the demo in `web/` shows the build works) and drawn through WebCodecs or a canvas, or in an app of our own. For delivery to standard players the output would have to be transcoded, so a codec of our own is a research track, not a product feature.

## A plan for a later run, with kill criteria

1. **Corpus and harness:** a few 1080p clips (camera video, animation, a screen recording), with `tools/video.py` measuring bits per second against SVT-AV1 (ffmpeg has it) and x265 at equal per-frame SSIMULACRA2 and VMAF, plus encode and decode time per frame.
2. **Motion search on the GPU:** a SAD kernel and a hierarchical search. Kept if it is at least 5× faster than 6 CPU threads at equal search quality.
3. **P frames:** motion-compensated prediction from the previous frame, residuals with the lossy experiment's transform and an adapted coefficient model, and in-loop deblocking. Kill criterion: within 25% of SVT-AV1 (preset 6, P frames only) at equal quality on the camera clips.
4. **Screen recordings:** the lossless coder with the previous frame as a copy source. Kill criterion: smaller than SVT-AV1 with its screen-content tools at visually lossless quality.
5. **Decode speed:** replace context mixing in inter frames with adaptive tANS. Kill criterion: 1080p at 30 frames per second on 4 threads.
6. **Segments:** I frames at fixed intervals and a CMAF-style segmenter, played back through the WebAssembly decoder in the demo page.
