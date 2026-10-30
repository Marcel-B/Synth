// Two small things the page does with script; everything else works without it.

// The hero's oscilloscope: a slowly morphing wave in the plugins' cyan and yellow, like their ScopeView.
(function scope() {
  const canvas = document.getElementById('scope');
  if (!canvas || !canvas.getContext) return;
  const context = canvas.getContext('2d');
  const still = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  let width = 0;
  let height = 0;

  function resize() {
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    width = canvas.clientWidth;
    height = canvas.clientHeight;
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
  }

  // A saw that a wavefolder bends further the higher `fold` goes, as Tonwerk Wavetable's warp does to a table.
  function wave(phase, fold) {
    const saw = 2 * (phase - Math.floor(phase)) - 1;
    return Math.sin(saw * (1 + fold * 3) * Math.PI / 2);
  }

  function draw(time) {
    const t = time / 1000;
    context.clearRect(0, 0, width, height);

    context.strokeStyle = 'rgba(42, 45, 58, 0.6)';
    context.lineWidth = 1;
    context.beginPath();
    for (let x = 0; x < width; x += 48) { context.moveTo(x + 0.5, 0); context.lineTo(x + 0.5, height); }
    for (let y = 0; y < height; y += 48) { context.moveTo(0, y + 0.5); context.lineTo(width, y + 0.5); }
    context.stroke();

    const layers = [
      { colour: 'rgba(0, 240, 255, 0.35)', fold: 0.5 + 0.5 * Math.sin(t * 0.31), shift: 0.0, amp: 0.26 },
      { colour: 'rgba(0, 240, 255, 0.18)', fold: 0.5 + 0.5 * Math.sin(t * 0.23 + 1), shift: 0.33, amp: 0.2 },
      { colour: 'rgba(252, 238, 10, 0.85)', fold: 0.5 + 0.5 * Math.sin(t * 0.4 + 2), shift: 0.66, amp: 0.3 },
    ];
    const cycles = Math.max(2, Math.round(width / 420));
    for (const layer of layers) {
      context.strokeStyle = layer.colour;
      context.lineWidth = layer === layers[2] ? 2 : 1.5;
      context.beginPath();
      for (let x = 0; x <= width; x += 2) {
        const phase = (x / width) * cycles + layer.shift + t * 0.05;
        const y = height * 0.62 - wave(phase, layer.fold) * height * layer.amp;
        if (x === 0) context.moveTo(x, y); else context.lineTo(x, y);
      }
      context.stroke();
    }
    if (!still) requestAnimationFrame(draw);
  }

  resize();
  window.addEventListener('resize', () => { resize(); if (still) draw(0); });
  if (still) draw(0); else requestAnimationFrame(draw);
})();

// A screenshot at full size in a dialog when its card's picture is clicked.
(function lightbox() {
  const dialog = document.getElementById('lightbox');
  if (!dialog || typeof dialog.showModal !== 'function') return;
  const image = dialog.querySelector('img');
  const title = dialog.querySelector('strong');
  // The same script serves the German pages and their English copies under en/.
  const english = document.documentElement.lang === 'en';

  document.querySelectorAll('.shot').forEach((button) => {
    button.addEventListener('click', () => {
      image.src = button.dataset.full;
      image.alt = (english ? 'Interface of ' : 'Oberfläche von ') + button.dataset.title;
      title.textContent = button.dataset.title;
      dialog.showModal();
    });
  });

  // A click on the backdrop closes it too.
  dialog.addEventListener('click', (event) => {
    if (event.target === dialog) dialog.close();
  });
})();
