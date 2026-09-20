(() => {
  const count = 10;
  const english = document.documentElement.lang.startsWith("en");
  const assetRoot = new URL(
    english ? "../assets/rotation/" : "assets/rotation/",
    location.href,
  );
  const sources = Array.from(
    { length: count },
    (_, i) => new URL(`trena-${String(i + 1).padStart(2, "0")}.webp?v=5`, assetRoot).href,
  );
  const names = english
    ? [
        "front",
        "front right",
        "right side",
        "rear right",
        "rear right",
        "rear",
        "rear left",
        "left side",
        "front left",
        "front left",
      ]
    : [
        "frontal",
        "frontal direita",
        "lateral direita",
        "traseira direita",
        "traseira direita",
        "traseira",
        "traseira esquerda",
        "lateral esquerda",
        "frontal esquerda",
        "frontal esquerda",
      ];

  const section = document.getElementById("explore");
  const product = document.getElementById("rotation-product");
  if (!section || !product) return;

  const reduced = matchMedia("(prefers-reduced-motion: reduce)");
  const frames = new Array(count);
  let requested = 0;
  let shown = -1;
  let scheduled = false;

  function render() {
    scheduled = false;
    if (!frames[requested] || shown === requested) return;
    shown = requested;
    product.src = frames[requested].src;
    product.alt = english
      ? `MM1-BLACK, ${names[requested]} view`
      : `MM1-BLACK, vista ${names[requested]}`;
  }

  function select(index) {
    requested = Math.max(0, Math.min(count - 1, Math.round(index)));
    if (!scheduled) {
      scheduled = true;
      requestAnimationFrame(render);
    }
  }

  function onScroll() {
    if (reduced.matches) return;
    const rect = section.getBoundingClientRect();
    const distance = Math.max(1, section.offsetHeight - innerHeight);
    const progress = Math.max(0, Math.min(1, -rect.top / distance));
    select(Math.min(count - 1, Math.floor(progress * count)));
  }

  let nextToLoad = 0;
  async function worker() {
    while (nextToLoad < count) {
      const index = nextToLoad++;
      const image = new Image();
      image.src = sources[index];
      try {
        await image.decode();
        frames[index] = image;
      } catch {}
      select(requested);
    }
  }

  worker();
  worker();
  addEventListener("scroll", onScroll, { passive: true });
  addEventListener("resize", onScroll);
  reduced.addEventListener("change", onScroll);
  onScroll();
})();
