/* eslint-disable */
export function drawDetectionBox (ctx, type, rect, class_name) {
  // ctx.lineWidth = 5;
  ctx.strokeStyle = type === 0 ? 'green' : (type === 1 ? 'red' : 'blue');
  ctx.strokeRect(rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]);
  ctx.fillStyle = 'red';
  if (class_name) {
    ctx.font = '16px Georgia';
    ctx.fillText(class_name, rect[0], rect[1]);
  }
}

export function drawLandmark (ctx, landmarks) {
  ctx.strokeStyle = "green";
  ctx.strokeWidth = 1;
  for (let i = 0; i < landmarks.length / 2; i++) {
    const x = landmarks[i * 2]
    const y = landmarks[i * 2 + 1]
    ctx.beginPath();
    ctx.arc(x, y, 1, 0, 2 * Math.PI, true);
    ctx.stroke(); 
  }
}

export function drawDetection (ctx, det) {
  drawDetectionBox(ctx, det.type, det.rect, det.labelString)
  if (det.landmarks && det.landmarks.length) {
    drawLandmark(ctx, det.landmarks);
  }
}

export function h264ProfileToCodec (profile) {
  switch (profile) {
    case 'Main':
      return 'avc1.4d0028';
    case 'Baseline':
      return 'avc1.42001e';
    case 'Constrained Baseline':
      return 'avc1.42001f';
    case 'Extended':
      return 'avc1.580028'
    case 'High':
      return 'avc1.640028'
    case 'High 10':
    case 'High 10 Intra':
      return 'avc1.6e0028'
    case 'High 4:2:2':
    case 'High 4:2:2 Intra':
      return 'avc1.7a0028'
    case 'High 4:4:4':
    case 'High 4:4:4 Predictive':
    case 'High 4:4:4 Intra':
      return 'avc1.f40028'
    default:
      return 'avc1.42001e';
  }
}

export class DetectionCanvas {
  constructor (canvas) {
    this.canvas = canvas
    this.ctx = canvas.getContext('2d');
  }

  drawBox (type, rect, class_name) {
    drawDetectionBox(this.ctx, type, rect, class_name)
  }

  drawLandmark (landmarks) {
    drawLandmark(this.ctx, landmarks);
  }

  drawDetection (det) {
    drawDetection(this.ctx, det);
  }

  clear () {
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
  }
}
