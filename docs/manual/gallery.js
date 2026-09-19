/* Static gallery — every MM1-BLACK screen for the manual catalog */
(() => {
  const chrome = (active, theme, body, opts = {}) => {
    const tabs = ["POINTS", "VIEW", "SENSOR", "FILE", "SETUP"];
    const bat = opts.bat ?? 78;
    const bt = opts.bt ?? "LINK";
    const wifi = opts.wifi ?? false;
    const aim = opts.hdrClass || "";
    return `
      <div class="gal-screen" data-theme="${theme || "light"}">
        <div class="dev-header">
          <span class="dev-title">MM1-BLACK</span>
          <div class="dev-icons">
            <span class="ico bat" style="color:${bat <= 15 ? "#C62828" : bat <= 30 ? "#F9A825" : "#2E7D32"}">${bat}%</span>
            <span class="ico sd on">SD</span>
            <span class="ico wifi${wifi ? " on" : ""}">WiFi</span>
            <span class="ico bt${bt === "LINK" ? " link" : ""}">${bt}</span>
            <span class="ico clock">14:32</span>
          </div>
        </div>
        <div class="dev-tabs">${tabs.map(t =>
          `<span class="dev-tab${t === active ? " active" : ""}">${t}</span>`
        ).join("")}</div>
        <div class="pane ${opts.paneClass || ""}">${body}</div>
      </div>`;
  };

  const screens = [
    {
      id: "splash",
      title: "Ao ligar",
      desc: "A trena mostra o logo MIRA por cerca de um segundo e abre na lista de pontos.",
      html: `<div class="gal-screen splash-only"><div class="splash-mira"><img src="assets/MIRA_Principal_R.svg" alt="MIRA"/><div>MM1-BLACK</div></div></div>`,
    },
    {
      id: "points",
      title: "POINTS — lista de pontos",
      desc: "Tela principal. Distância, direção e inclinação. DEL apaga · CLR limpa a tela · SAVE grava o levantamento · TX envia ao celular.",
      html: chrome("POINTS", "light", `
        <table class="pts-table"><thead><tr><th>Ref#</th><th>D(m)</th><th>E</th><th>Azm</th><th>Inc</th></tr></thead>
        <tbody>
          <tr class="sel"><td class="c-ref">12</td><td>4.812</td><td></td><td>212.4</td><td>-3.1</td></tr>
          <tr class="nav"><td class="c-ref">11</td><td>3.105</td><td></td><td>210.8</td><td>-2.8</td></tr>
          <tr class="nav"><td class="c-ref">10</td><td>3.105</td><td></td><td>210.8</td><td>-2.8</td></tr>
          <tr class="nav"><td class="c-ref">9</td><td>3.105</td><td></td><td>210.8</td><td>-2.8</td></tr>
          <tr><td class="c-ref">8</td><td>2.447</td><td></td><td>98.2</td><td>1.4</td></tr>
          <tr><td class="c-ref">7</td><td>5.920</td><td></td><td>45.0</td><td>-12.6</td></tr>
          <tr><td class="c-ref">6</td><td>-</td><td class="c-e">E</td><td>-</td><td>-</td></tr>
        </tbody></table>
        <div class="pager"><span>‹</span><span>12 pts  1/1</span><span>›</span></div>
        <div class="action-bar"><span class="ab del">DEL</span><span class="ab clr">CLR</span><span class="ab save">SAVE</span><span class="ab tx">TX</span></div>
        <div class="status-line">Ready</div>`),
      details: [
        ["Ref#", "Índice sequencial do tiro na sessão/arquivo"],
        ["D(m)", "Distância em metros (3 casas na tabela); já com trim aplicado"],
        ["E", "Marca E se a leitura do laser falhou"],
        ["Azm / Inc", "Azimute e inclinação em graus (1 casa)"],
        ["DEL", "Apaga só a linha selecionada na RAM"],
        ["CLR", "Limpa a lista da tela; o que já foi gravado com SAVE permanece"],
        ["SAVE", "Grava o levantamento. Sem SAVE, não grava."],
        ["TX", "Abre STREAM BLE (até 5000 pernas)"],
      ],
    },
    {
      id: "points-aim",
      title: "2-tap — primeiro clique",
      desc: "O cabeçalho fica âmbar e o laser liga. Clique de novo para capturar.",
      html: chrome("POINTS", "light", `
        <table class="pts-table"><thead class="aim"><tr><th>Ref#</th><th>D(m)</th><th>E</th><th>Azm</th><th>Inc</th></tr></thead>
        <tbody>
          <tr><td class="c-ref">12</td><td>4.812</td><td></td><td>212.4</td><td>-3.1</td></tr>
          <tr><td class="c-ref">8</td><td>2.447</td><td></td><td>98.2</td><td>1.4</td></tr>
        </tbody></table>
        <div class="pager"><span>‹</span><span>12 pts  1/1</span><span>›</span></div>
        <div class="action-bar"><span class="ab del">DEL</span><span class="ab clr">CLR</span><span class="ab save">SAVE</span><span class="ab tx">TX</span></div>
        <div class="status-line">AIM - press again to CAPTURE</div>`, { hdrClass: "aim" }),
    },
    {
      id: "points-ok",
      title: "2-tap — segundo clique",
      desc: "Cabeçalho verde: captura ok. Vermelho: falha. Um som confirma.",
      html: chrome("POINTS", "light", `
        <table class="pts-table"><thead class="ok"><tr><th>Ref#</th><th>D(m)</th><th>E</th><th>Azm</th><th>Inc</th></tr></thead>
        <tbody>
          <tr class="sel"><td class="c-ref">13</td><td>3.245</td><td></td><td>188.2</td><td>-1.4</td></tr>
          <tr><td class="c-ref">12</td><td>4.812</td><td></td><td>212.4</td><td>-3.1</td></tr>
        </tbody></table>
        <div class="pager"><span>‹</span><span>13 pts  1/1</span><span>›</span></div>
        <div class="action-bar"><span class="ab del">DEL</span><span class="ab clr">CLR</span><span class="ab save">SAVE</span><span class="ab tx">TX</span></div>
        <div class="status-line">Shot #13  3.245 m</div>`),
    },
    {
      id: "stream",
      title: "Enviar para o celular (TX)",
      desc: "Enquanto a trena manda os pontos ao app, aparece o progresso. Você pode cancelar a qualquer momento.",
      html: chrome("POINTS", "light", `
        <div class="stream-modal">
          <div class="stream-title">STREAM BLE</div>
          <div class="stream-body">SD 128/420  30%</div>
          <div class="stream-bar"><i style="width:30%"></i></div>
          <span class="ab del" style="display:block;text-align:center;margin-top:10px">Cancel</span>
        </div>`),
    },
    {
      id: "view",
      title: "VIEW — planta + perfil (exemplo)",
      desc: "Acompanhe a captura em tempo real ou, ao carregar um arquivo, veja o layout das capturas. PLAN = planta. PROFILE = perfil.",
      html: chrome("VIEW", "light", `
        <div class="plot-card">
          <div class="plot-title">PLAN  azi   2 leg</div>
          <svg viewBox="0 0 220 120" class="plot">
            <defs><marker id="arr" markerWidth="6" markerHeight="6" refX="5" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6 Z" fill="#0B5CAB"/></marker></defs>
            <line x1="18" y1="100" x2="205" y2="100" stroke="currentColor" stroke-width=".6" opacity=".35"/>
            <line x1="18" y1="100" x2="18" y2="12" stroke="currentColor" stroke-width=".6" opacity=".35"/>
            <!-- station A -->
            <circle cx="40" cy="85" r="4" fill="#2E7D32"/>
            <text x="46" y="82" font-size="7" fill="currentColor">A</text>
            <!-- splays from A -->
            <line x1="40" y1="85" x2="70" y2="55" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="40" y1="85" x2="28" y2="50" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="40" y1="85" x2="85" y2="90" stroke="#90A4AE" stroke-width="1.2"/>
            <circle cx="70" cy="55" r="2" fill="#90A4AE"/>
            <circle cx="28" cy="50" r="2" fill="#90A4AE"/>
            <circle cx="85" cy="90" r="2" fill="#90A4AE"/>
            <!-- leg A→B (nav) -->
            <line x1="40" y1="85" x2="130" y2="48" stroke="#0B5CAB" stroke-width="2.4" marker-end="url(#arr)"/>
            <!-- station B -->
            <circle cx="130" cy="48" r="4" fill="#2E7D32"/>
            <text x="136" y="45" font-size="7" fill="currentColor">B</text>
            <!-- splays from B -->
            <line x1="130" y1="48" x2="165" y2="30" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="130" y1="48" x2="175" y2="60" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="130" y1="48" x2="155" y2="75" stroke="#90A4AE" stroke-width="1.2"/>
            <circle cx="165" cy="30" r="2" fill="#90A4AE"/>
            <circle cx="175" cy="60" r="2" fill="#90A4AE"/>
            <circle cx="155" cy="75" r="2" fill="#90A4AE"/>
            <!-- failed splay -->
            <line x1="130" y1="48" x2="110" y2="25" stroke="#C62828" stroke-width="1.2" stroke-dasharray="3 2"/>
            <circle cx="110" cy="25" r="2" fill="#C62828"/>
            <text x="160" y="112" font-size="8" fill="currentColor">5 m</text>
            <text x="20" y="14" font-size="7" fill="#4A6270">N ↑</text>
          </svg>
        </div>
        <div class="plot-card">
          <div class="plot-title">PROFILE  inc   6 splay</div>
          <svg viewBox="0 0 220 120" class="plot">
            <line x1="18" y1="60" x2="205" y2="60" stroke="currentColor" stroke-width=".6" opacity=".35"/>
            <line x1="18" y1="105" x2="18" y2="15" stroke="currentColor" stroke-width=".6" opacity=".35"/>
            <circle cx="35" cy="62" r="4" fill="#2E7D32"/>
            <text x="30" y="78" font-size="7" fill="currentColor">A</text>
            <line x1="35" y1="62" x2="55" y2="48" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="35" y1="62" x2="60" y2="78" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="35" y1="62" x2="70" y2="58" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="35" y1="62" x2="120" y2="42" stroke="#00796B" stroke-width="2.4"/>
            <circle cx="120" cy="42" r="4" fill="#2E7D32"/>
            <text x="115" y="58" font-size="7" fill="currentColor">B</text>
            <line x1="120" y1="42" x2="150" y2="28" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="120" y1="42" x2="165" y2="50" stroke="#90A4AE" stroke-width="1.2"/>
            <line x1="120" y1="42" x2="155" y2="65" stroke="#90A4AE" stroke-width="1.2"/>
            <circle cx="55" cy="48" r="2" fill="#90A4AE"/>
            <circle cx="60" cy="78" r="2" fill="#90A4AE"/>
            <circle cx="150" cy="28" r="2" fill="#90A4AE"/>
            <text x="160" y="112" font-size="8" fill="currentColor">5 m</text>
            <text x="20" y="20" font-size="7" fill="#4A6270">Up</text>
          </svg>
        </div>`),
      details: [
        ["Ponto verde", "Estação (após Nav×3)"],
        ["Linha azul / teal espessa", "Perna (leg) entre estações"],
        ["Linha cinza fina", "Splay (Sample Regular)"],
        ["Linha vermelha tracejada", "Tiro com falha de laser"],
        ["Título PLAN", "Conta legs válidas"],
        ["Título PROFILE", "Conta splays"],
        ["Escala", "Barra em metros, auto-fit com margem 10%"],
      ],
    },
    {
      id: "sensor",
      title: "SENSOR — diagnóstico ao vivo",
      desc: "Diagnóstico ao vivo na trena. Laser atualiza ~1 Hz nesta aba. Valide IMU, botão e bateria antes do survey.",
      html: chrome("SENSOR", "light", `
        <div class="sensor">
          <div class="scard c-lzr"><div class="sc-h">LASER</div><div class="sc-v">3.245 m</div></div>
          <div class="scard c-imu"><div class="sc-h">IMU</div><div class="sc-v">Az 212.4°<br>Inc -3.1°<br>Roll 1.2°</div></div>
          <div class="scard c-lnk"><div class="sc-h">LINK</div><div class="sc-v">laser OK<br>imu OK<br>rx 1842</div></div>
          <div class="scard c-btn"><div class="sc-h">BUTTON</div><div class="sc-v">2-tap ready</div></div>
          <div class="scard c-tmp"><div class="sc-h">TEMP / BAT</div><div class="sc-v">38.2 °C<br>3.91 V · 78%</div></div>
        </div>`, { paneClass: "sensor-pane" }),
      details: [
        ["LASER", "Distância usada (com trim); ou no reading"],
        ["IMU", "Az / Inc / Roll; ou offline + texto de scan"],
        ["LINK", "laser/imu OK|FAIL e contador UART rx"],
        ["BUTTON", "2-tap/1-tap/cont ready · AIM — tap again · CONT running · pressed · not wired"],
        ["TEMP / BAT", "Temperatura MCU °C e tensão/ % da bateria"],
      ],
    },
    {
      id: "file",
      title: "FILE — arquivos CSV",
      desc: "Lista até 16 arquivos no SD. Arquivo ativo com fundo verde. NEW cria mm1_black_XXX.csv; USE carrega; DEL remove.",
      html: chrome("FILE", "light", `
        <div class="file-active">Active: mm1_black_003.csv  (12 pts)</div>
        <table class="file-table"><thead><tr><th>File</th><th>Size</th></tr></thead>
        <tbody>
          <tr class="act"><td>mm1_black_003.csv</td><td>4.2 KB</td></tr>
          <tr><td>mm1_black_002.csv</td><td>18.7 KB</td></tr>
          <tr><td>mm1_black_001.csv</td><td>9.1 KB</td></tr>
          <tr><td>mm1_black_000.csv</td><td>2.4 KB</td></tr>
        </tbody></table>
        <div class="action-bar"><span class="ab new">NEW</span><span class="ab use">USE</span><span class="ab del">DEL</span></div>
        <div class="status-line">Ready</div>`),
      details: [
        ["Active", "Arquivo em uso + contagem de pontos em RAM"],
        ["NEW", "Próximo índice livre mm1_black_XXX.csv"],
        ["USE", "Torna o selecionado ativo e carrega pontos"],
        ["DEL", "Apaga do SD; se era o ativo, troca/recria 000"],
        ["Status", "Ready, Loading..., Counting..., erros de SD"],
      ],
    },
    {
      id: "bright",
      title: "Brilho e som",
      desc: "Ajuste o brilho da tela, o volume dos bipes e o tema claro ou escuro.",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub on">Bright</span><span class="sub">Cal</span><span class="sub">BT</span><span class="sub">WiFi</span><span class="sub">About</span>
        </div>
        <div class="setup-body">
          <label class="slider-lab">Display brightness</label>
          <div class="fake-slider"><i style="width:80%"></i></div>
          <div class="slider-val">Brightness 80%</div>
          <label class="slider-lab">Speaker volume</label>
          <div class="fake-slider"><i style="width:40%"></i></div>
          <div class="slider-val">40%</div>
          <label class="slider-lab">Theme</label>
          <div class="seg"><span class="seg-btn on">Light</span><span class="seg-btn">Dark</span></div>
          <div class="hint">Saved automatically when you release the slider.</div>
        </div>`),
    },
    {
      id: "dark",
      title: "Tema escuro",
      desc: "Opção mais confortável em ambientes escuros. Ative em SETUP → Bright → Dark.",
      html: chrome("POINTS", "dark", `
        <table class="pts-table"><thead><tr><th>Ref#</th><th>D(m)</th><th>E</th><th>Azm</th><th>Inc</th></tr></thead>
        <tbody>
          <tr class="sel"><td class="c-ref">12</td><td>4.812</td><td></td><td>212.4</td><td>-3.1</td></tr>
          <tr class="nav"><td class="c-ref">11</td><td>3.105</td><td></td><td>210.8</td><td>-2.8</td></tr>
          <tr><td class="c-ref">8</td><td>2.447</td><td></td><td>98.2</td><td>1.4</td></tr>
        </tbody></table>
        <div class="action-bar"><span class="ab del">DEL</span><span class="ab clr">CLR</span><span class="ab save">SAVE</span><span class="ab tx">TX</span></div>`),
    },
    {
      id: "cal-menu",
      title: "Menu de calibração",
      desc: "Aqui você testa o laser, ajusta a bússola, corrige a distância e escolhe como o botão funciona.",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub">Bright</span><span class="sub on">Cal</span><span class="sub">BT</span><span class="sub">WiFi</span><span class="sub">About</span>
        </div>
        <div class="cal-menu">
          <div class="cal-tile"><strong>IMU</strong><span>Health, quality, heading trim</span></div>
          <div class="cal-tile"><strong>Laser</strong><span>Distance, test, health</span></div>
          <div class="cal-tile"><strong>Trim</strong><span>Laser offset (mm)</span></div>
          <div class="cal-tile"><strong>Measure</strong><span>Capture button mode</span></div>
        </div>`),
    },
    {
      id: "cal-imu",
      title: "Bússola e direção (IMU)",
      desc: "Confira se a direção está boa. Se estiver estranha, afaste-se de metal e use os ajustes desta tela.",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub">Bright</span><span class="sub on">Cal</span><span class="sub">BT</span><span class="sub">WiFi</span><span class="sub">About</span>
        </div>
        <div class="setup-body">
          <button type="button" class="back-cal">‹ Cal</button>
          <div class="bt-status">IMU</div>
          <div class="row-kv"><span>Health</span><span>OK</span></div>
          <div class="row-kv"><span>Quality</span><span>good</span></div>
          <div class="row-kv"><span>Consistency</span><span>OK (g=0.99)</span></div>
          <div class="row-kv"><span>Heading</span><span>212.4  incl -3.1</span></div>
          <div class="row-kv"><span>Azimuth</span><span>+0.0 deg</span></div>
          <div class="hint">Type heading trim (deg), then OK.</div>
          <div class="big-metric">0.0</div>
          <div class="numpad-fake">7 8 9 · 4 5 6 · 1 2 3 · +/- 0 . · Del OK</div>
          <div class="seg" style="margin-top:6px"><span class="seg-btn on">Head=0</span><span class="seg-btn">Default</span></div>
          <div class="hint">If heading drifts, figure-8 for ~30 s away from metal.</div>
        </div>`),
      details: [
        ["Head=0", "Define offset para que o rumo atual vire 0°"],
        ["Default", "Restaura offset de fábrica/build"],
        ["Numpad OK", "Grava trim de azimute em NVS"],
        ["Figure-8", "Recalibração magnética ~30 s longe de metal"],
      ],
    },
    {
      id: "cal-laser",
      title: "Teste do laser",
      desc: "Veja a distância ao vivo e use Test para confirmar se a leitura está correta.",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub">Bright</span><span class="sub on">Cal</span><span class="sub">BT</span><span class="sub">WiFi</span><span class="sub">About</span>
        </div>
        <div class="setup-body">
          <button type="button" class="back-cal">‹ Cal</button>
          <div class="bt-status">Laser</div>
          <div class="big-metric">3.245 m</div>
          <div class="row-kv"><span>Health</span><span>OK · rx 1842</span></div>
          <div class="hint">Aim at a bright target (&gt;10 cm). Use Test to check distance.</div>
          <span class="ab tx" style="display:block;text-align:center">Test</span>
        </div>`),
    },
    {
      id: "cal-trim",
      title: "Ajuste fino da distância",
      desc: "Se a trena mede sempre um pouco a mais ou a menos, corrija aqui (em milímetros).",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub">Bright</span><span class="sub on">Cal</span><span class="sub">BT</span><span class="sub">WiFi</span><span class="sub">About</span>
        </div>
        <div class="setup-body">
          <button type="button" class="back-cal">‹ Cal</button>
          <div class="bt-status">Laser trim</div>
          <div class="row-kv"><span>Laser</span><span>3.245 m</span></div>
          <div class="row-kv"><span>Used</span><span>3.250 m</span></div>
          <div class="hint">Offset (mm), then OK. Applies to all shots.</div>
          <div class="big-metric">+5</div>
          <div class="numpad-fake">7 8 9 · 4 5 6 · 1 2 3 · +/- 0 . · Del OK</div>
          <span class="ab clr" style="display:block;text-align:center;margin-top:6px">Rst</span>
        </div>`),
    },
    {
      id: "cal-measure",
      title: "Como o botão funciona",
      desc: "Escolha 2-tap (mira + medida), 1-tap (rápido) ou contínuo. Ligue o Hold 5 s para avançar de estação.",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub">Bright</span><span class="sub on">Cal</span><span class="sub">BT</span><span class="sub">WiFi</span><span class="sub">About</span>
        </div>
        <div class="setup-body">
          <button type="button" class="back-cal">‹ Cal</button>
          <div class="bt-status">Measure button</div>
          <div class="seg triple">
            <span class="seg-btn on">2-tap</span><span class="seg-btn">1-tap</span><span class="seg-btn">Cont</span>
          </div>
          <span class="hold-btn on" style="display:block;text-align:center;margin-top:10px">Hold 5s nav ON</span>
          <div class="hint">2-tap: aim then capture. 1-tap: one press. Cont: tap start/stop; TopoDroid 5% gate, never nav. Hold 5s (1/2-tap only) writes nav x3.</div>
        </div>`),
    },
    {
      id: "bt",
      title: "Bluetooth com o celular",
      desc: "Conecte o TopoDroid ou SexyTopo. Quando aparecer LINK, as medições podem ir para o app.",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub">Bright</span><span class="sub">Cal</span><span class="sub on">BT</span><span class="sub">WiFi</span><span class="sub">About</span>
        </div>
        <div class="setup-body">
          <div class="bt-status">Bluetooth: SAP6 connected</div>
          <div class="row-kv"><span>Name</span><span>SAP6_0001</span></div>
          <div class="row-kv"><span>MAC</span><span>A4:C1:38:···</span></div>
          <div class="row-kv"><span>Bond</span><span>saved</span></div>
          <div class="row-kv"><span>Peer</span><span>TopoDroid</span></div>
          <div class="row-kv"><span>Diag</span><span>legs 12 · ACK 12 · q 0</span></div>
          <div class="action-bar wrap" style="margin-top:8px">
            <span class="ab tx">Measure</span><span class="ab save">TX</span>
            <span class="ab use">Restart BLE</span><span class="ab del">Unpair</span>
          </div>
        </div>`, { bt: "LINK" }),
      details: [
        ["Measure", "Um Sample + notificação BLE se linkado"],
        ["TX", "STREAM do CSV/pernas"],
        ["Restart BLE", "Reinicia stack / advertising"],
        ["Unpair", "Remove bond; precisa parear de novo"],
      ],
    },
    {
      id: "wifi",
      title: "Wi‑Fi e atualização",
      desc: "Opcional: conectar a uma rede para atualizar a trena, ou criar a rede MM1-MIRA para baixar arquivos.",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub">Bright</span><span class="sub">Cal</span><span class="sub">BT</span><span class="sub on">WiFi</span><span class="sub">About</span>
        </div>
        <div class="setup-body">
          <div class="fw-banner">New firmware v1.0.8<br/>Tap Install to update.</div>
          <div class="row-kv"><span>Network</span><span>Connected to MIRA-Lab</span></div>
          <div class="field">MIRA-Lab</div>
          <div class="field">••••••••</div>
          <div class="ssid-chips"><span>MIRA-Lab</span><span>cave-ap</span><span>Guest</span></div>
          <div class="action-bar wrap">
            <span class="ab del">Off</span><span class="ab use">Join</span><span class="ab tx">Scan</span>
          </div>
          <div class="action-bar wrap">
            <span class="ab save">Check</span><span class="ab new">Install</span><span class="ab use">AP</span>
          </div>
          <div class="hint">SoftAP: MM1-MIRA / mira-mm1 — portal for file export</div>
        </div>`, { wifi: true }),
      details: [
        ["Off", "Desliga rádio Wi‑Fi"],
        ["Join", "Conecta ao SSID (salvo em NVS)"],
        ["Scan", "Lista redes; toque num chip e Join"],
        ["Check", "Consulta releases no GitHub"],
        ["Install", "OTA; exige bateria ok e Check prévio"],
        ["AP", "SoftAP + portal HTTP para exportar arquivos"],
      ],
    },
    {
      id: "about",
      title: "Sobre a trena",
      desc: "Nome do produto, versão do software e QR Code para atualização.",
      html: chrome("SETUP", "light", `
        <div class="setup-subs">
          <span class="sub">Bright</span><span class="sub">Cal</span><span class="sub">BT</span><span class="sub">WiFi</span><span class="sub on">About</span>
        </div>
        <div class="setup-body about">
          <div class="about-brand">MIRA Robotica</div>
          <div class="about-url">https://www.mirarobotica.com/</div>
          <div class="about-prod">MM1-BLACK</div>
          <div class="about-ver">v1.0.7</div>
          <div class="hint">Check for updates in SETUP → WiFi.</div>
          <div class="qr-fake">QR</div>
        </div>`),
    },
  ];

  const FEATURED = {
    points: "fig-points",
    view: "fig-view",
    sensor: "fig-sensor",
    file: "fig-file",
    "points-aim": "fig-tap1",
    "points-ok": "fig-tap2",
  };

  const CONSULTA_IDS = new Set([
    "stream", "bright", "dark",
    "cal-menu", "cal-imu", "cal-laser", "cal-trim", "cal-measure",
    "bt", "wifi", "about", "splash",
  ]);

  function mount() {
    // Figuras ao lado do texto principal
    Object.entries(FEATURED).forEach(([id, slotId]) => {
      const slot = document.getElementById(slotId);
      const screen = screens.find(s => s.id === id);
      if (slot && screen) slot.innerHTML = screen.html;
    });

    // Anexo: só telas de consulta (sem repetir as 4 principais)
    const root = document.getElementById("screen-catalog");
    if (!root) return;
    const list = screens.filter(s => CONSULTA_IDS.has(s.id));
    root.innerHTML = list.map((s, i) => `
      <article class="gal-card" id="tela-${s.id}">
        <div class="gal-device">${s.html}</div>
        <div class="gal-copy">
          <p class="gal-num">${String(i + 1).padStart(2, "0")}</p>
          <h3>${s.title}</h3>
          <p>${s.desc}</p>
        </div>
      </article>`).join("");
  }

  document.addEventListener("DOMContentLoaded", mount);
})();
