'use strict';

// studytrack - interface. Sem framework de proposito: a pagina inteira cabe
// num arquivo e a prioridade e registrar uma sessao em menos de 20 segundos.

const $  = (s) => document.querySelector(s);
const $$ = (s) => Array.from(document.querySelectorAll(s));

let estado = null;                   // resposta de /api/state
const selecionados = new Set();      // ids de no marcados na fila
const resultados = new Map();        // id -> 'worked' | 'good' | 'struggled'
let cron = null;                     // { materia, inicio, timer }

const ROTULOS = {
  todo: 'a fazer', learning: 'estudando', consolidated: 'consolidado',
  maintenance: 'manutenção', locked: 'travado', archived: 'arquivado'
};

const METRICAS = { fagote: ['bpm', 'BPM atingido'], animacao: ['frames', 'frames feitos'], cpp: ['problems', 'problemas resolvidos'] };

// ---------------------------------------------------------------------------

async function carregar() {
  const r = await fetch('/api/state');
  if (!r.ok) { $('#fila').innerHTML = '<p class="vazio">falha ao carregar</p>'; return; }
  estado = await r.json();
  $('#data').textContent = formatarData(estado.today);
  desenharFila();
  desenharTrilhas();
}

function formatarData(iso) {
  const [a, m, d] = iso.split('-').map(Number);
  const dt = new Date(a, m - 1, d);
  return dt.toLocaleDateString('pt-BR', { weekday: 'short', day: 'numeric', month: 'short' });
}

function materia(id) { return estado.subjects.find((s) => s.id === id) || { name: id, color: '#888' }; }
function noPorId(id) { return estado.nodes.find((n) => n.id === id); }

// ---------------------------------------------------------------------------

function desenharFila() {
  const alvo = $('#fila');
  if (!estado.queue.length) {
    alvo.innerHTML = '<p class="vazio">Nada na fila hoje.<br>Tudo em dia — e isso é um resultado, não um bug.</p>';
    return;
  }

  alvo.innerHTML = '';
  for (const item of estado.queue) {
    const s = materia(item.subject);
    const no = noPorId(item.node_id);

    const el = document.createElement('article');
    el.className = 'cartao' + (selecionados.has(item.node_id) ? ' sel' : '');
    el.style.setProperty('--cor', s.color);

    const etiqueta = item.reason === 'prazo' ? 'etiqueta prazo' : 'etiqueta';
    const detalhe = item.detail ? ` · ${item.detail}` : '';

    el.innerHTML = `
      <div class="cartao-topo">
        <span class="marca">&#10003;</span>
        <div style="flex:1;min-width:0">
          <h3></h3>
          <div class="meta">
            <span class="${etiqueta}"></span>
            <span></span>
          </div>
        </div>
      </div>`;

    // textContent em vez de innerHTML: titulos e criterios vem do seed e
    // podem conter qualquer caractere
    el.querySelector('h3').textContent = item.title;
    el.querySelector('.etiqueta').textContent = item.reason;
    el.querySelector('.meta span:last-child').textContent =
      `${s.name} · ${item.minutes} min${detalhe}`;

    if (no && no.criterion) {
      const p = document.createElement('p');
      p.className = 'criterio';
      p.textContent = no.criterion;
      el.appendChild(p);
    }

    el.addEventListener('click', () => alternar(item.node_id));
    alvo.appendChild(el);
  }
}

function alternar(id) {
  if (selecionados.has(id)) { selecionados.delete(id); resultados.delete(id); }
  else { selecionados.add(id); resultados.set(id, 'worked'); }
  desenharFila();
  atualizarBarra();
}

function atualizarBarra() {
  const n = selecionados.size;
  $('#barra').classList.toggle('oculto', n === 0);
  if (n === 0) return;

  const min = Array.from(selecionados)
    .map((id) => (noPorId(id) || {}).est_minutes || 0)
    .reduce((a, b) => a + b, 0);
  $('#barra-info').textContent = `${n} ${n === 1 ? 'item' : 'itens'} · ~${min} min`;
}

// ---------------------------------------------------------------------------

function desenharTrilhas() {
  const alvo = $('#trilhas');
  alvo.innerHTML = '';

  for (const s of estado.subjects) {
    const nos = estado.nodes.filter((n) => n.subject_id === s.id);
    const sec = document.createElement('section');
    sec.className = 'materia';

    const h = document.createElement('h2');
    h.textContent = s.name;
    h.style.color = s.color;
    sec.appendChild(h);

    const meta = document.createElement('div');
    meta.className = 'materia-meta';
    const pronto = nos.filter((n) => n.state === 'consolidated' || n.state === 'maintenance').length;
    meta.textContent = `${nos.length} nós · ${pronto} dominados · ${s.session_count} sessões · ${s.weekly_minutes} min/semana`;
    sec.appendChild(meta);

    const ordem = { learning: 0, maintenance: 1, todo: 2, consolidated: 3, locked: 4, archived: 5 };
    nos.sort((a, b) => (ordem[a.state] - ordem[b.state]) || a.title.localeCompare(b.title, 'pt-BR'));

    for (const n of nos) sec.appendChild(linhaNo(n));
    alvo.appendChild(sec);
  }
}

function linhaNo(n) {
  const el = document.createElement('div');
  el.className = 'no' + (n.unlocked ? '' : ' travado');

  const cab = document.createElement('div');
  cab.className = 'no-cab';

  const nome = document.createElement('span');
  nome.className = 'nome';
  nome.textContent = n.title;

  const pip = document.createElement('span');
  pip.className = 'pip ' + n.state;
  pip.textContent = n.unlocked ? (ROTULOS[n.state] || n.state) : 'travado';

  cab.append(nome, pip);
  el.appendChild(cab);

  let aberto = false;
  el.addEventListener('click', (ev) => {
    if (ev.target.tagName === 'BUTTON') return;
    aberto = !aberto;
    const velho = el.querySelector('.detalhe');
    if (velho) velho.remove();
    if (aberto) el.appendChild(detalheNo(n));
  });

  return el;
}

function detalheNo(n) {
  const d = document.createElement('div');
  d.className = 'detalhe';

  if (n.criterion) {
    const p = document.createElement('p');
    p.innerHTML = '<strong>Critério:</strong> ';
    p.append(document.createTextNode(n.criterion));
    d.appendChild(p);
  }
  if (n.notes) {
    const p = document.createElement('p');
    p.textContent = n.notes;
    d.appendChild(p);
  }
  if (n.missing && n.missing.length) {
    const p = document.createElement('p');
    p.innerHTML = '<strong>Falta antes:</strong> ';
    p.append(document.createTextNode(
      n.missing.map((id) => (noPorId(id) || { title: id }).title).join(', ')));
    d.appendChild(p);
  }
  if (n.deadline) {
    const p = document.createElement('p');
    p.innerHTML = '<strong>Prazo:</strong> ';
    p.append(document.createTextNode(n.deadline));
    d.appendChild(p);
  }
  if (n.srs_due_on) {
    const p = document.createElement('p');
    p.textContent = `Próxima revisão: ${n.srs_due_on}`;
    d.appendChild(p);
  }

  const acoes = document.createElement('div');
  acoes.className = 'estados';
  for (const [chave, texto] of [['learning', 'estudando'], ['consolidated', 'consolidado'],
                                ['maintenance', 'manutenção'], ['todo', 'a fazer'],
                                ['archived', 'arquivar']]) {
    if (chave === n.state) continue;
    const b = document.createElement('button');
    b.className = 'btn-pequeno';
    b.textContent = texto;
    b.addEventListener('click', () => mudarEstado(n.id, chave));
    acoes.appendChild(b);
  }
  d.appendChild(acoes);
  return d;
}

async function mudarEstado(id, novo) {
  const r = await fetch('/api/node/state', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id, state: novo })
  });
  if (r.ok) { aviso('estado atualizado'); await carregar(); }
  else aviso('falhou');
}

// ---------------------------------------------------------------------------
// registro de sessão

function abrirFolha() {
  if (!selecionados.size) return;

  const ids = Array.from(selecionados);
  const mats = new Set(ids.map((id) => (noPorId(id) || {}).subject_id));
  if (mats.size > 1) {
    aviso('uma sessão por matéria — separe os itens');
    return;
  }

  const min = ids.map((id) => (noPorId(id) || {}).est_minutes || 0).reduce((a, b) => a + b, 0);
  $('#dur').value = cron ? Math.max(1, Math.round((Date.now() - cron.inicio) / 60000)) : min;

  const lista = $('#lista-nos');
  lista.innerHTML = '';
  for (const id of ids) {
    const n = noPorId(id);
    const linha = document.createElement('div');
    linha.className = 'no-linha';

    const t = document.createElement('div');
    t.className = 'no-titulo';
    t.textContent = n ? n.title : id;
    linha.appendChild(t);

    const ops = document.createElement('div');
    ops.className = 'opcoes';
    for (const [chave, texto] of [['struggled', 'travei'], ['worked', 'ok'], ['good', 'fluiu']]) {
      const b = document.createElement('button');
      b.className = 'opcao' + (resultados.get(id) === chave ? ' on' : '');
      b.textContent = texto;
      b.addEventListener('click', () => {
        resultados.set(id, chave);
        Array.from(ops.children).forEach((c) => c.classList.remove('on'));
        b.classList.add('on');
      });
      ops.appendChild(b);
    }
    linha.appendChild(ops);
    lista.appendChild(linha);
  }

  const mat = mats.values().next().value;
  const m = METRICAS[mat];
  $('#campo-metrica').classList.toggle('oculto', !m);
  if (m) { $('#metrica-nome').textContent = m[1]; $('#metrica-valor').value = ''; }
  $('#campo-metrica').dataset.chave = m ? m[0] : '';

  $('#erro').textContent = '';
  $('#folha').classList.remove('oculto');
}

async function salvar() {
  const ids = Array.from(selecionados);
  if (!ids.length) return;

  const dur = parseInt($('#dur').value, 10);
  if (!dur || dur < 1) { $('#erro').textContent = 'informe a duração'; return; }

  const mat = (noPorId(ids[0]) || {}).subject_id;
  const corpo = {
    subject_id: mat,
    duration_min: dur,
    note: $('#nota').value.trim(),
    started_at: new Date().toISOString().slice(0, 19).replace('T', ' '),
    nodes: ids.map((id) => ({ id, outcome: resultados.get(id) || 'worked' })),
    metrics: {}
  };

  const chave = $('#campo-metrica').dataset.chave;
  const valor = parseFloat($('#metrica-valor').value);
  if (chave && !Number.isNaN(valor)) corpo.metrics[chave] = valor;

  $('#salvar').disabled = true;
  try {
    const r = await fetch('/api/session', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(corpo)
    });
    const j = await r.json();
    if (!r.ok) throw new Error(j.error || 'falhou');

    fecharFolha();
    selecionados.clear();
    resultados.clear();
    pararCron();
    $('#nota').value = '';
    atualizarBarra();
    await carregar();
    aviso('sessão registrada');
  } catch (e) {
    $('#erro').textContent = e.message;
  } finally {
    $('#salvar').disabled = false;
  }
}

function fecharFolha() { $('#folha').classList.add('oculto'); }

// ---------------------------------------------------------------------------
// cronômetro

function iniciarCron(mat) {
  cron = { materia: mat, inicio: Date.now(), timer: null };
  $('#cron-materia').textContent = materia(mat).name;
  $('#cronometro').classList.remove('oculto');
  cron.timer = setInterval(() => {
    const s = Math.floor((Date.now() - cron.inicio) / 1000);
    $('#cron-tempo').textContent =
      String(Math.floor(s / 60)).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
  }, 1000);
}

function pararCron() {
  if (cron && cron.timer) clearInterval(cron.timer);
  cron = null;
  $('#cronometro').classList.add('oculto');
  $('#cron-tempo').textContent = '00:00';
}

// ---------------------------------------------------------------------------

let toastTimer = null;
function aviso(texto) {
  const t = $('#toast');
  t.textContent = texto;
  t.classList.add('on');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('on'), 2200);
}

$$('.aba').forEach((b) => b.addEventListener('click', () => {
  $$('.aba').forEach((x) => x.classList.remove('ativa'));
  b.classList.add('ativa');
  const alvo = b.dataset.aba;
  $('#painel-hoje').classList.toggle('oculto', alvo !== 'hoje');
  $('#painel-trilhas').classList.toggle('oculto', alvo !== 'trilhas');
}));

$('#btn-registrar').addEventListener('click', abrirFolha);
$('#folha-fechar').addEventListener('click', fecharFolha);
$('#salvar').addEventListener('click', salvar);
$('#cron-parar').addEventListener('click', () => { pararCron(); abrirFolha(); });
$('#folha').addEventListener('click', (e) => { if (e.target.id === 'folha') fecharFolha(); });

// iniciar cronômetro com toque longo no primeiro item selecionado da matéria
$('#fila').addEventListener('contextmenu', (e) => {
  const cartao = e.target.closest('.cartao');
  if (!cartao) return;
  e.preventDefault();
  const idx = Array.from($('#fila').children).indexOf(cartao);
  const item = estado.queue[idx];
  if (item) { iniciarCron(item.subject); aviso('cronômetro iniciado'); }
});

carregar();
