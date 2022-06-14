import './App.css';
import Webcam from 'react-webcam';
import { useCallback, useEffect, useRef, useState } from 'react';
import { useAnimateFrame } from './hooks';
import { FPSStats } from './FPSStats';

const jsfeat = window.jsfeat;
const width = 640, height = 480;
// const width = 256, height = 192;
const boxW = width / 2.5 | 0, boxH = height / 2.5 | 0;
// const boxW = width / 1 | 0, boxH = height / 1 | 0;
const box = [(width - boxW) / 2 | 0, (height - boxH) / 2 | 0, boxW, boxH];
const getBoxCenter = (b = box) => [b[0] + boxW / 2, b[1] + boxH / 2];
const setBoxCenter = (x, y, b = box) => { b[0] = x - boxW / 2; b[1] = y - boxH / 2; };
const options = {
  win_size: 10,
  max_iterations: 15,
  epsilon: 0.01,
  min_eigen: 0.0015,
};
let curr_img_pyr = new jsfeat.pyramid_t(2);
let prev_img_pyr = new jsfeat.pyramid_t(2);
curr_img_pyr.allocate(width, height, jsfeat.U8C1_t);
prev_img_pyr.allocate(width, height, jsfeat.U8C1_t);
const max_allowed_oflow_points = 5;
const point_status = new Uint8Array(max_allowed_oflow_points);
let prev_xy = new Float32Array(max_allowed_oflow_points * 2);
let curr_xy = new Float32Array(max_allowed_oflow_points * 2);
let point_count = 0;

const targetAlpha = new Uint8ClampedArray(boxW * boxW);
targetAlpha.fill(255);

function draw_circle(ctx, x, y) {
  ctx.beginPath();
  ctx.arc(x, y, 4, 0, Math.PI * 2, true);
  ctx.closePath();
  ctx.fill();
}
function prune_oflow_points(ctx) {
  let j = 0;
  for (let i = 0; i < point_count; ++i) {
    if (point_status[i] !== 1) continue;
    if (j < i) {
      curr_xy[j << 1] = curr_xy[i << 1];
      curr_xy[(j << 1) + 1] = curr_xy[(i << 1) + 1];
    }
    draw_circle(ctx, curr_xy[j << 1], curr_xy[(j << 1) + 1]);
    ++j;
  }
  point_count = j;
}
function add_point(x, y) {
  if (x > 0 & y > 0 & x < width & y < height) {
    curr_xy[point_count << 1] = x;
    curr_xy[(point_count << 1) + 1] = y;
    point_count++;
  }
}

const sendImg = base64 => new Promise((s, r) => {
  let data = new FormData();
  data.append("base64", base64);
  let xhr = new XMLHttpRequest();
  xhr.open("POST", window.location.protocol + "//" + window.location.hostname + ":5000/segmentation", true);
  xhr.onreadystatechange = function ( response ) {
    if(response.target.readyState === XMLHttpRequest.DONE) {
      let status = response.target.status;
      if (status === 0 || (status >= 200 && status < 400))
        s(response.target.responseText.replace(/-/g, "+").replace(/_/g, "/"));
      else r(status);
    }
  };
  xhr.onerror = r;
  xhr.send(data);
});

const tcanvas = document.createElement("canvas");
const aplhaMap = [];
for (let i = 0, n = 10, m = 64, {log} = Math, b = log(n+1), dm = 255-m; i < 256; ++i)
  aplhaMap[i] = (log(i / 255 * n + 1) / b) * dm + m;

export default function App() {
  const webcamRef = useRef(null), canvasRef = useRef(null);
  const outputRef = useRef(null), tempCanvasRef = useRef(document.createElement("canvas"));
  const asdf = useRef(null);
  const [start, setStart] = useState(false);

  function drawBox(ctx) {
    ctx.strokeStyle = 'blue';
    ctx.lineWidth = 2.5;
    ctx.strokeRect(...box);
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    let [x, y, w, h] = box;
    ctx.moveTo(x, y);
    ctx.lineTo(x + w, y + h);
    ctx.moveTo(x + w, y);
    ctx.lineTo(x, y + h);
    ctx.stroke();
  }

  useEffect(() => {
    if (!start) return () => {};
    let flag = true, timer = null;
    async function communication() {
      if (!flag) return;
      const webcam = webcamRef.current, video = webcamRef.current?.video;
      const canvas = canvasRef.current, outputCanvas = outputRef.current;
      if (!canvas || !outputCanvas || video?.readyState !== 4) return;
      const tc = tempCanvasRef.current;
      const ctx = tc.getContext("2d");
      asdf.current.onload = () => {
        tcanvas.width = boxW; tcanvas.height = boxH;
        const ctx = tcanvas.getContext("2d");
        // ctx.drawImage(asdf.current, 0, 0, width, height);
        // let imgData = ctx.getImageData(0, 0, width, height);
        ctx.drawImage(asdf.current, 0, 0, boxW, boxH);
        let imgData = ctx.getImageData(0, 0, boxW, boxH);
        for (let i = 0; i < imgData.data.length; i += 4)
          targetAlpha[i / 4] = aplhaMap[imgData.data[i]];
      };
      tc.width = width; tc.height = height;
      ctx.drawImage(webcam.getCanvas({ width, height, }), 0, 0, width, height);
      let boxImg = ctx.getImageData(...box);
      tc.width = boxW; tc.height = boxH;
      ctx.putImageData(boxImg, 0, 0, 0, 0, boxW, boxH);
      asdf.current.src = await sendImg(tc.toDataURL());
      timer = setTimeout(communication, 0);
    }
    timer = setTimeout(communication, 0);
    return () => { flag = false; clearTimeout(timer); };
  }, [start]);

  useAnimateFrame((_, dt) => {
    const webcam = webcamRef.current, video = webcamRef.current?.video;
    const canvas = canvasRef.current, outputCanvas = outputRef.current;
    if (!canvas || !outputCanvas || video?.readyState !== 4) return;
    if (!canvas.ctx) {
      canvas.ctx = canvas.getContext('2d');
      outputCanvas.width = video.videoWidth;
      outputCanvas.height = video.videoHeight;
      canvas.width = video.videoWidth;
      canvas.height = video.videoHeight;
      outputCanvas.style.width = canvas.clientWidth + "px";
      outputCanvas.style.height = canvas.clientHeight + "px";
    }
    canvas.width = video.videoWidth;
    canvas.height = video.videoHeight;

    const ctx = canvas.ctx;
    ctx.fillStyle = "#0f0";
    let c = webcam.getCanvas({ width, height, });
    ctx.drawImage(c, 0, 0, width, height);

    if (!start) return drawBox(ctx);
    if (!outputCanvas.ctx) {
      outputCanvas.ctx = outputCanvas.getContext('2d');
    }
    let imgData = ctx.getImageData(0, 0, width, height);
    const outputCtx = outputCanvas.ctx;
    let outputImgData = outputCtx.getImageData(0, 0, width, height);
    let od = outputImgData.data, id = imgData.data;
    for (let j = 0; j < height; ++j) {
      for (let i = 0; i < width; ++i) {
        let t = j * width + i, bt = t * 4;
        od[bt + 0] = id[bt + 0];
        od[bt + 1] = id[bt + 1];
        od[bt + 2] = id[bt + 2];
        if (i < box[0] || i >= box[0] + boxW || j < box[1] || j >= box[1] + boxH) {
          let now = od[bt + 3], target = 64, d = target - now;
          if (Math.abs(d) > 8) d = now < target? 8: now > target? -8: 0;
          od[bt + 3] = now + d;
        }
        else {
          // let target = targetAlpha[t];
          let target = targetAlpha[((j - box[1] | 0) * boxW) + (i - box[0] | 0)];
          let now = od[bt + 3], d = target - now;
          if (Math.abs(d) > 5) d = now < target? 5: now > target? -5: 0;
          od[bt + 3] = now + d;
        }
      }
    }
    outputCtx.putImageData(outputImgData, 0, 0, 0, 0, width, height);

    // Keep track of the center point of the box visible to the user through the streamer method.

    if (point_count === 0) {
      const ws = options.win_size;
      let [cx, cy] = getBoxCenter();
      cx = Math.min(width - ws, Math.max(ws, cx));
      cy = Math.min(height - ws, Math.max(ws, cy));
      add_point(cx, cy);
    }
    [prev_xy, curr_xy] = [curr_xy, prev_xy];
    [prev_img_pyr, curr_img_pyr] = [curr_img_pyr, prev_img_pyr];

    jsfeat.imgproc.grayscale(imgData.data, width, height, curr_img_pyr.data[0]);

    curr_img_pyr.build(curr_img_pyr.data[0], true);
    jsfeat.optical_flow_lk.track(
      prev_img_pyr, curr_img_pyr, prev_xy, curr_xy, point_count,
      options.win_size|0, options.max_iterations|0, point_status, options.epsilon, options.min_eigen
    );

    prune_oflow_points(ctx);
    setBoxCenter(curr_xy[0], curr_xy[1]);
    drawBox(ctx);
  }, [start]);

  const handleClick = useCallback(() => {
    setStart(!start);
  }, [start]);

  return <div className='camera-echo'>
    <FPSStats />
    <div className='info'>
      <h3>Place the item to be scanned in the center of the box. Click the <b>Start</b> button when ready.</h3>
      <button onClick={handleClick}>{start? "Stop": "Start"}</button>
    </div>
    <fieldset className='input-field'>
      <legend>Input</legend>
      <Webcam
        className='echo'
        ref={webcamRef}
        mirrored
        width={width} height={height}
        videoConstraints={{
          width, height, audio: false,
        }}
      />
      <canvas className='canvas' ref={canvasRef} />
    </fieldset>
    <fieldset className='output-field'>
      <legend>Output</legend>
      <canvas className='output-canvas' ref={outputRef}></canvas>
      <img ref={asdf} />
    </fieldset>
  </div>;
};
