// ============================================================
//  MagicRings — Three.js WebGL shader animation (vanilla JS)
//  React component-аас хөрвүүлсэн. react-bits/MagicRings-д
//  суурилсан shader ашиглана.
// ============================================================

(function initMagicRings() {
  const mount = document.getElementById('magicRingsMount');
  if (!mount || typeof THREE === 'undefined') return;

  // ---------- Config ----------
  const CFG = {
    color:          '#c8860a',
    colorTwo:       '#e8a020',
    speed:          1,
    ringCount:      5,
    attenuation:    10,
    lineThickness:  2,
    baseRadius:     0.55,
    radiusStep:     0.1,
    scaleRate:      0.1,
    opacity:        0.75,
    noiseAmount:    0.1,
    rotation:       0,
    ringGap:        1.5,
    fadeIn:         0.7,
    fadeOut:        0.5,
    followMouse:    false,
    mouseInfluence: 0.2,
    hoverScale:     1.2,
    parallax:       0.05,
    clickBurst:     false,
  };

  // ---------- Shaders (оригиналтай ижил) ----------
  const vertexShader = `
void main() {
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}`;

  const fragmentShader = `
precision highp float;

uniform float uTime, uAttenuation, uLineThickness;
uniform float uBaseRadius, uRadiusStep, uScaleRate;
uniform float uOpacity, uNoiseAmount, uRotation, uRingGap;
uniform float uFadeIn, uFadeOut;
uniform float uMouseInfluence, uHoverAmount, uHoverScale, uParallax, uBurst;
uniform vec2  uResolution, uMouse;
uniform vec3  uColor, uColorTwo;
uniform int   uRingCount;

const float HP    = 1.5707963;
const float CYCLE = 3.45;

float fade(float t) {
  return t < uFadeIn
    ? smoothstep(0.0, uFadeIn, t)
    : 1.0 - smoothstep(uFadeOut, CYCLE - 0.2, t);
}

float ring(vec2 p, float ri, float cut, float t0, float px) {
  float t  = mod(uTime + t0, CYCLE);
  float r  = ri + t / CYCLE * uScaleRate;
  float d  = abs(length(p) - r);
  float a  = atan(abs(p.y), abs(p.x)) / HP;
  float th = max(1.0 - a, 0.5) * px * uLineThickness;
  float h  = (1.0 - smoothstep(th, th * 1.5, d)) + 1.0;
  d += pow(cut * a, 3.0) * r;
  return h * exp(-uAttenuation * d) * fade(t);
}

void main() {
  float px = 1.0 / min(uResolution.x, uResolution.y);
  vec2  p  = (gl_FragCoord.xy - 0.5 * uResolution.xy) * px;
  float cr = cos(uRotation), sr = sin(uRotation);
  p = mat2(cr, -sr, sr, cr) * p;
  p -= uMouse * uMouseInfluence;
  float sc = mix(1.0, uHoverScale, uHoverAmount) + uBurst * 0.3;
  p /= sc;

  vec3  c   = vec3(0.0);
  float rcf = max(float(uRingCount) - 1.0, 1.0);

  for (int i = 0; i < 10; i++) {
    if (i >= uRingCount) break;
    float fi = float(i);
    vec2  pr = p - fi * uParallax * uMouse;
    vec3  rc = mix(uColor, uColorTwo, fi / rcf);
    c = mix(c, rc, vec3(ring(pr,
          uBaseRadius + fi * uRadiusStep,
          pow(uRingGap, fi),
          i == 0 ? 0.0 : 2.95 * fi,
          px)));
  }

  c *= 1.0 + uBurst * 2.0;
  float n = fract(sin(dot(gl_FragCoord.xy + uTime * 100.0,
                          vec2(12.9898, 78.233))) * 43758.5453);
  c += (n - 0.5) * uNoiseAmount;
  gl_FragColor = vec4(c, max(c.r, max(c.g, c.b)) * uOpacity);
}`;

  // ---------- Renderer ----------
  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ alpha: true, antialias: false });
  } catch (e) { return; }

  renderer.setClearColor(0x000000, 0);
  mount.appendChild(renderer.domElement);

  const scene  = new THREE.Scene();
  const camera = new THREE.OrthographicCamera(-0.5, 0.5, 0.5, -0.5, 0.1, 10);
  camera.position.z = 1;

  // ---------- Uniforms ----------
  const U = {
    uTime:          { value: 0 },
    uAttenuation:   { value: CFG.attenuation },
    uResolution:    { value: new THREE.Vector2() },
    uColor:         { value: new THREE.Color(CFG.color) },
    uColorTwo:      { value: new THREE.Color(CFG.colorTwo) },
    uLineThickness: { value: CFG.lineThickness },
    uBaseRadius:    { value: CFG.baseRadius },
    uRadiusStep:    { value: CFG.radiusStep },
    uScaleRate:     { value: CFG.scaleRate },
    uRingCount:     { value: CFG.ringCount },
    uOpacity:       { value: CFG.opacity },
    uNoiseAmount:   { value: CFG.noiseAmount },
    uRotation:      { value: 0 },
    uRingGap:       { value: CFG.ringGap },
    uFadeIn:        { value: CFG.fadeIn },
    uFadeOut:       { value: CFG.fadeOut },
    uMouse:         { value: new THREE.Vector2() },
    uMouseInfluence:{ value: 0 },
    uHoverAmount:   { value: 0 },
    uHoverScale:    { value: CFG.hoverScale },
    uParallax:      { value: CFG.parallax },
    uBurst:         { value: 0 },
  };

  const material = new THREE.ShaderMaterial({
    vertexShader, fragmentShader, uniforms: U, transparent: true,
  });
  scene.add(new THREE.Mesh(new THREE.PlaneGeometry(1, 1), material));

  // ---------- Resize ----------
  function resize() {
    const w   = mount.clientWidth;
    const h   = mount.clientHeight;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    renderer.setSize(w, h);
    renderer.setPixelRatio(dpr);
    U.uResolution.value.set(w * dpr, h * dpr);
  }
  resize();
  window.addEventListener('resize', resize);

  const ro = new ResizeObserver(resize);
  ro.observe(mount);

  // ---------- Mouse ----------
  let mouse  = [0, 0];
  let smooth = [0, 0];
  let hover  = 0;
  let isHov  = false;
  let burst  = 0;

  mount.addEventListener('mousemove', e => {
    const r = mount.getBoundingClientRect();
    mouse[0] =  (e.clientX - r.left) / r.width  - 0.5;
    mouse[1] = -((e.clientY - r.top)  / r.height - 0.5);
  });
  mount.addEventListener('mouseenter', () => { isHov = true; });
  mount.addEventListener('mouseleave', () => { isHov = false; mouse[0] = 0; mouse[1] = 0; });
  mount.addEventListener('click', () => { if (CFG.clickBurst) burst = 1; });

  // ---------- Animate ----------
  let raf;
  function animate(t) {
    raf = requestAnimationFrame(animate);

    smooth[0] += (mouse[0] - smooth[0]) * 0.08;
    smooth[1] += (mouse[1] - smooth[1]) * 0.08;
    hover      += ((isHov ? 1 : 0) - hover) * 0.08;
    burst      *= 0.95;
    if (burst < 0.001) burst = 0;

    U.uTime.value          = t * 0.001 * CFG.speed;
    U.uMouse.value.set(smooth[0], smooth[1]);
    U.uMouseInfluence.value = CFG.followMouse ? CFG.mouseInfluence : 0;
    U.uHoverAmount.value    = hover;
    U.uBurst.value          = burst;
    U.uRotation.value       = (CFG.rotation * Math.PI) / 180;

    renderer.render(scene, camera);
  }
  raf = requestAnimationFrame(animate);

  // Page unload дээр цэвэрлэнэ
  window.addEventListener('beforeunload', () => {
    cancelAnimationFrame(raf);
    window.removeEventListener('resize', resize);
    ro.disconnect();
    renderer.dispose();
    material.dispose();
  });
})();
