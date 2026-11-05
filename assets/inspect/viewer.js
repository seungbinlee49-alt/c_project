'use strict';
const data = JSON.parse(document.getElementById('data').textContent);
const $ = id => document.getElementById(id), canvas = $('canvas'), ctx = canvas.getContext('2d');
const state = {
    pair : null,
    edge : -1,
    renderToken : 0
};
const number = (n, d = 3) => n == null ? '—' : Number(n).toFixed(d);
const escapeHtml = s => String(s).replace(
    /[&<>"']/g,
    c => ({'&' : '&amp;', '<' : '&lt;', '>' : '&gt;', '"' : '&quot;', "'" : '&#39;'}[c]));
const imageCache = new Map();
function image(entry) {
    if (!entry.url)
        return Promise.resolve(null);
    if (!imageCache.has(entry.url))
        imageCache.set(entry.url, new Promise((resolve, reject) => {
                           const img = new Image();
                           img.onload = () => resolve(img);
                           img.onerror = () =>
                               reject(new Error('이미지 파일을 읽지 못했습니다: ' + entry.name));
                           img.src = entry.dataUrl || entry.url;
                       }));
    return imageCache.get(entry.url);
}
$('run').textContent = `${data.run} · 조건 ${data.noise} · 시드 ${data.seed} · 전체 ${
    data.plannedPairs}쌍 중 진단용 선택 ${data.pairs.length}쌍`;
function visiblePairs() {
    const query = $('search').value.toLowerCase(), filter = $('filter').value;
    return data.pairs.filter(p => {
        const a = data.images[p.a], b = data.images[p.b];
        return (filter === 'all' || filter === 'genuine' && p.genuine ||
                filter === 'impostor' && !p.genuine || filter === 'failed' && p.status !== 'OK') &&
               `${p.id} ${a.name} ${b.name}`.toLowerCase().includes(query);
    });
}
function list() {
    const pairs = visiblePairs();
    $('pairs').replaceChildren();
    $('count').textContent = `${pairs.length}쌍 표시`;
    for (const p of pairs) {
        const button = document.createElement('button');
        button.className = 'pair' + (state.pair === p ? ' active' : '');
        button.dataset.pairId = p.id;
        button.innerHTML = `<span class="badge">${
            p.genuine ? '본인 · 낮은 점수 우선' : '타인 · 높은 점수 우선'} · #${
            escapeHtml(p.id)}</span><br>${escapeHtml(data.images[p.a].name)} → ${
            escapeHtml(
                data.images[p.b].name)}<br><span class="${p.status === 'OK' ? 'badge' : 'fail'}">${
            p.status === 'OK' ? '점수 ' + number(p.score, 6)
                              : escapeHtml(p.status) + ' / ' + escapeHtml(p.stage)}</span>`;
        button.onclick = () => select(p);
        $('pairs').append(button);
    }
    if (!pairs.length)
        $('pairs').textContent = '일치하는 비교가 없습니다.';
}
function select(p) {
    state.pair = p;
    state.edge = -1;
    list();
    const a = data.images[p.a], b = data.images[p.b], t = p.transform,
          distances = p.matches.map(e => e[3]);
    $('title').textContent = `${p.genuine ? '본인 비교' : '타인 비교'} #${p.id}`;
    $('subtitle').textContent = `${p.database} · ${a.name} → ${b.name} · ${p.status}${
        p.status !== 'OK' ? ' (' + p.stage + ')' : ''}`;
    const rmse = distances.length
                     ? Math.sqrt(distances.reduce((s, x) => s + x * x, 0) / distances.length)
                     : null;
    const stats = [
        [ '기록된 유사도', number(p.score, 6) ],
        [ '추출 특징점', `${a.points.length} / ${b.points.length}` ],
        [ '선택된 대응', p.matches.length ],
        [ '회전', p.hasAlignment ? number(Math.atan2(t[1], t[0]) * 180 / Math.PI, 1) + '°' : '—' ],
        [ '위치 RMSE', rmse == null ? '—' : number(rmse, 2) + ' px' ]
    ];
    $('stats').innerHTML =
        stats
            .map(([ k, v ]) =>
                     `<div class="stat"><span class="muted">${k}</span><strong>${v}</strong></div>`)
            .join('');
    $('leftCaption').textContent = `등록 · ${a.name} · ${a.width}×${a.height}`;
    $('rightCaption').textContent = `비교 · ${b.name} · ${data.noise}`;
    const warnings = [ '표시한 대응은 알고리즘의 선택이며, 사람이 확인한 정답 대응이 아닙니다.' ];
    if (p.status !== 'OK')
        warnings.push(`이 비교는 ${p.stage} 단계에서 ${
            p.status}로 기록되었습니다. 유효한 정합을 표시하지 않습니다.`);
    else if (!p.hasAlignment)
        warnings.push('점수는 정상 반환되었지만, 유효한 정합 가설이 남지 않았습니다.');
    if (data.noise !== 'clean')
        warnings.push(
            '비교 이미지는 저장된 조건·시드로 잡음을 재생성했습니다. 동일한 난수·변형 구현을 전제로 합니다.');
    if (a.outOfBounds || b.outOfBounds)
        warnings.push(`이미지 영역 밖 특징점: 등록 ${a.outOfBounds}, 비교 ${
            b.outOfBounds}. 좌표 변환을 확인해야 합니다.`);
    if (a.decodeError || b.decodeError)
        warnings.push('이미지 표시 실패: ' +
                      [ a.decodeError, b.decodeError ].filter(Boolean).join(' / '));
    $('warning').textContent = warnings.join(' ');
    const observations = [
        `등록 특징 추출: ${a.extraction.status}, 비교 특징 추출: ${b.extraction.status}.`,
        p.status === 'OK'
            ? `국소 후보 ${p.candidateCount}개에서 정합 가설 ${p.hypotheses}개를 검사했습니다.`
            : '처리 실패 기록이므로 정합 과정의 재계산을 생략했습니다.'
    ];
    if (p.hasAlignment) {
        observations.push(
            `등록→비교 이동: x ${number(t[2], 2)} px, y ${number(t[3], 2)} px.`,
            `선택된 대응 비율: 등록 기준 ${
                number(100 * p.matches.length / Math.max(1, a.points.length), 1)}%, 비교 기준 ${
                number(100 * p.matches.length / Math.max(1, b.points.length), 1)}%.`);
    }
    if (!p.genuine && p.hasAlignment)
        observations.push(
            '서로 다른 지문에서도 기하학적으로 맞아 보이는 일부 특징점이 생길 수 있습니다. 높은 점수의 원인을 확인하는 개발 사례입니다.');
    if (p.genuine && p.status === 'OK')
        observations.push(
            '낮은 본인 점수만으로 추출 오류와 정합 오류를 확정할 수 없습니다. 대응되지 않은 영역과 특징점의 위치·방향을 함께 확인하세요.');
    $('observations').replaceChildren(...observations.map(s => {
        const li = document.createElement('li');
        li.textContent = s;
        return li;
    }));
    $('matches').replaceChildren();
    p.matches.forEach((e, index) => {
        const tr = document.createElement('tr');
        tr.innerHTML = `<td>${e[0]}</td><td>${e[1]}</td><td>${number(e[2], 4)}</td><td>${
            number(e[3], 3)}</td><td>${number(e[4], 2)}</td>`;
        tr.onclick = () => {
            state.edge = state.edge === index ? -1 : index;
            highlightRows();
            render().then(() => canvas.scrollIntoView({block : 'center', behavior : 'auto'}));
        };
        $('matches').append(tr);
    });
    $('verification').textContent =
        `원본 이미지 SHA-256 확인 완료. 정상 비교의 재계산 점수와 기록 점수 차이 ≤ 10⁻¹². 시험 출처 해시: ${
            data.provenanceSha256}`;
    $('extraction').textContent = `추출 기록: 등록 오류 ${a.extraction.error_code} ${
        a.extraction.message}; 비교 오류 ${b.extraction.error_code} ${b.extraction.message}`;
    $('view').options[1].disabled = !p.hasAlignment;
    if (!p.hasAlignment)
        $('view').value = 'split';
    render();
}
function highlightRows() {
    [...$('matches').children].forEach((tr, i) =>
                                           tr.classList.toggle('selected', i === state.edge));
}
async function render() {
    const token = ++state.renderToken, p = state.pair;
    if (!p)
        return;
    canvas.dataset.ready = 'false';
    const a = data.images[p.a], b = data.images[p.b];
    let ia, ib;
    try {
        [ia, ib] = await Promise.all([ image(a), image(b) ]);
    } catch (error) {
        if (token === state.renderToken)
            $('warning').textContent = error.message;
        return;
    }
    if (token !== state.renderToken)
        return;
    const aligned = $('view').value === 'aligned', gap = 55, pad = 25,
          w = aligned ? b.width + pad * 2 : a.width + b.width + gap + pad * 2,
          h = (aligned ? b.height : Math.max(a.height, b.height)) + pad * 2;
    canvas.width = w * 2;
    canvas.height = h * 2;
    ctx.setTransform(2, 0, 0, 2, 0, 0);
    ctx.fillStyle = '#111c26';
    ctx.fillRect(0, 0, w, h);
    ctx.translate(pad, pad);
    const tx = aligned ? 0 : a.width + gap;
    if (aligned) {
        if (ib)
            ctx.drawImage(ib, 0, 0);
        if (ia) {
            ctx.save();
            ctx.beginPath();
            ctx.rect(0, 0, b.width, b.height);
            ctx.clip();
            ctx.globalAlpha = 0.45;
            const t = p.transform;
            ctx.transform(t[0], t[1], -t[1], t[0], t[2], t[3]);
            ctx.drawImage(ia, 0, 0);
            ctx.restore();
        }
    } else {
        if (ia)
            ctx.drawImage(ia, 0, 0);
        if (ib)
            ctx.drawImage(ib, tx, 0);
    }
    if (aligned) {
        ctx.beginPath();
        ctx.rect(0, 0, b.width, b.height);
        ctx.clip();
    }
    const minQuality = Number($('quality').value) / 100,
          acceptedA = new Set(p.matches.map(e => e[0])),
          acceptedB = new Set(p.matches.map(e => e[1]));
    function position(point, first) {
        if (aligned && first) {
            const t = p.transform;
            return [
                t[0] * point[0] - t[1] * point[1] + t[2], t[1] * point[0] + t[0] * point[1] + t[3],
                point[2] + Math.atan2(t[1], t[0])
            ];
        }
        return [ point[0] + (first ? 0 : tx), point[1], point[2] ];
    }
    function visible(edge) {
        return a.points[edge[0]][3] >= minQuality && b.points[edge[1]][3] >= minQuality;
    }
    if ($('lines').checked)
        for (let i = 0; i < p.matches.length; i++) {
            const edge = p.matches[i];
            if (!visible(edge))
                continue;
            const aa = position(a.points[edge[0]], true), bb = position(b.points[edge[1]], false);
            ctx.beginPath();
            ctx.moveTo(aa[0], aa[1]);
            ctx.lineTo(bb[0], bb[1]);
            ctx.strokeStyle = state.edge === i ? '#d35fdf'
                              : aligned        ? 'rgba(0,160,135,.9)'
                                               : 'rgba(0,110,130,.55)';
            ctx.lineWidth = state.edge === i ? 2.5 : 1;
            ctx.stroke();
        }
    if ($('points').checked)
        for (const [entry, first, accepted] of [[ a, true, acceptedA ], [ b, false, acceptedB ]])
            entry.points.forEach((point, i) => {
                if (point[3] < minQuality)
                    return;
                const [x, y, angle] = position(point, first);
                const edge = p.matches[state.edge],
                      selected = edge && (first ? edge[0] : edge[1]) === i;
                ctx.strokeStyle = selected ? '#d35fdf' : accepted.has(i) ? '#00a88b' : '#ed8d35';
                ctx.lineWidth = selected ? 2.5 : 1.5;
                ctx.beginPath();
                ctx.arc(x, y, selected ? 5 : 3.3, 0, Math.PI * 2);
                ctx.moveTo(x, y);
                ctx.lineTo(x + 12 * Math.cos(angle), y + 12 * Math.sin(angle));
                ctx.stroke();
                if ($('ids').checked) {
                    ctx.fillStyle = '#001c2b';
                    ctx.font = '10px monospace';
                    ctx.fillText(String(i), x + 5, y - 5);
                }
            });
    canvas.dataset.pair = p.id;
    canvas.dataset.view = $('view').value;
    canvas.dataset.ready = 'true';
}
for (const id of ['filter', 'search'])
    $(id).addEventListener(id === 'search' ? 'input' : 'change', list);
for (const id of ['view', 'points', 'lines', 'ids'])
    $(id).addEventListener('change', render);
$('quality').oninput = () => {
    $('qualityValue').textContent = $('quality').value;
    render();
};
$('reset').onclick = () => {
    state.edge = -1;
    highlightRows();
    render();
};
$('export').onclick = () => {
    if (!state.pair)
        return;
    const link = document.createElement('a');
    link.download = `pair-${state.pair.id}-${$('view').value}.png`;
    link.href = canvas.toDataURL('image/png');
    link.click();
};
list();
if (data.pairs.length)
    select(data.pairs[0]);
else
    $('title').textContent = '표시할 비교가 없습니다';
