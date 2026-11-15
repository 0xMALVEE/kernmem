import React, { useCallback, useEffect, useRef, useState } from 'react'
import { createRoot } from 'react-dom/client'
import './style.css'

const POLL_MS = 250
const PREVIEW_WIDTH = 430

function num(v, digits = 2) {
  return typeof v === 'number' ? v.toFixed(digits) : '—'
}

function hex(v) {
  return typeof v === 'number' ? '0x' + v.toString(16).toUpperCase() : '—'
}

function Screen({ conv, client }) {
  const ref = useRef(null)

  useEffect(() => {
    const canvas = ref.current
    if (!canvas || !client || !client[0]) return

    const scale = PREVIEW_WIDTH / client[0]
    const w = Math.round(client[0] * scale)
    const h = Math.round(client[1] * scale)
    const dpr = window.devicePixelRatio || 1

    canvas.width = Math.round(w * dpr)
    canvas.height = Math.round(h * dpr)
    canvas.style.width = w + 'px'
    canvas.style.height = h + 'px'

    const ctx = canvas.getContext('2d')
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.fillStyle = '#0a0c10'
    ctx.fillRect(0, 0, w, h)

    ctx.strokeStyle = '#1b2230'
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1)
    ctx.beginPath()
    ctx.moveTo(w / 2, 0)
    ctx.lineTo(w / 2, h)
    ctx.moveTo(0, h / 2)
    ctx.lineTo(w, h / 2)
    ctx.stroke()

    const ring = conv.ring || []
    if (ring.length > 1) {
      ctx.strokeStyle = '#59c8ff'
      ctx.lineWidth = 1.4
      ctx.beginPath()
      ring.forEach((p, i) => (i === 0 ? ctx.moveTo(p[0] * scale, p[1] * scale) : ctx.lineTo(p[0] * scale, p[1] * scale)))
      ctx.stroke()
    }

    if (conv.self) {
      ctx.fillStyle = '#7dff7d'
      ctx.beginPath()
      ctx.arc(conv.self[0] * scale, conv.self[1] * scale, 3.5, 0, Math.PI * 2)
      ctx.fill()
    }

    ctx.font = '10px ui-monospace, monospace'
    ;(conv.heroes || []).forEach((hero) => {
      if (!hero.screen) return
      const x = hero.screen[0] * scale
      const y = hero.screen[1] * scale
      ctx.strokeStyle = '#ff5a5a'
      ctx.strokeRect(x - 4, y - 7, 8, 14)
      ctx.fillStyle = '#ffb4b4'
      ctx.fillText(hero.name || '?', x + 6, y)
    })
  }, [conv, client])

  return <canvas ref={ref} className="screen" />
}

function App() {
  const [state, setState] = useState(null)
  const [scan, setScan] = useState(null)
  const [feed, setFeed] = useState('connecting')

  useEffect(() => {
    let alive = true
    const tick = async () => {
      try {
        const res = await fetch('/api/state', { cache: 'no-store' })
        const json = await res.json()
        if (!alive) return
        setState(json)
        setFeed('live')
      } catch (err) {
        if (alive) setFeed('api unreachable - is server.py running?')
      }
    }
    tick()
    const id = setInterval(tick, POLL_MS)
    return () => {
      alive = false
      clearInterval(id)
    }
  }, [])

  const runScan = useCallback(async () => {
    setScan({ busy: true })
    try {
      const res = await fetch('/api/scan', { cache: 'no-store' })
      setScan(await res.json())
    } catch (err) {
      setScan({ error: String(err) })
    }
  }, [])

  const client = state && state.client
  const conventions = (state && state.conventions) || []
  const positions = (state && state.positions) || []
  const heroes = (state && state.heroes) || []
  const signature = (state && state.signature) || []

  return (
    <div className="app">
      <header className="bar">
        <span className={feed === 'live' ? 'pill ok' : 'pill bad'}>{feed}</span>
        <span className="mono">{state && state.error ? state.error : 'streaming'}</span>
        <span className="spacer" />
        <span className={state && state.focused ? 'pill ok' : 'pill warn'}>
          {state && state.focused ? 'game focused' : 'game NOT focused (camera freezes)'}
        </span>
        <span className="mono dim">pid {state ? state.pid : '—'} · base {state ? hex(state.base) : '—'}</span>
      </header>

      <div className="grid">
        <section className="panel">
          <h2>raw</h2>
          <dl className="kv">
            <dt>device</dt>
            <dd className="mono">{state && state.device ? state.device : 'not open'}</dd>
            <dt>client</dt>
            <dd className="mono">{client ? client[0] + ' x ' + client[1] : '—'}</dd>
            <dt>local player</dt>
            <dd className="mono">{state ? hex(state.local) : '—'}</dd>
            <dt>team</dt>
            <dd className="mono">{state ? String(state.team) : '—'}</dd>
            <dt>attack range</dt>
            <dd className="mono">{state ? num(state.range, 1) : '—'}</dd>
            <dt>camera offset</dt>
            <dd className="mono">{state && state.camera != null ? hex(state.camera) : '—'}</dd>
            <dt>view crc</dt>
            <dd className="mono">{state && state.viewCrc != null ? state.viewCrc.toString(16).toUpperCase() : '—'}</dd>
            <dt>signature hits</dt>
            <dd className="mono">{signature.length ? signature.map(hex).join(', ') : 'none'}</dd>
          </dl>

          <h2>position candidates</h2>
          <table className="table">
            <thead>
              <tr>
                <th>offset</th>
                <th>x</th>
                <th>y</th>
                <th>z</th>
              </tr>
            </thead>
            <tbody>
              {positions.map((p) => (
                <tr key={p.offset}>
                  <td className="mono">{hex(p.offset)}</td>
                  <td className="mono">{p.value ? num(p.value[0]) : '—'}</td>
                  <td className="mono">{p.value ? num(p.value[1]) : '—'}</td>
                  <td className="mono">{p.value ? num(p.value[2]) : '—'}</td>
                </tr>
              ))}
              {!positions.length && (
                <tr>
                  <td colSpan="4" className="dim">no local player</td>
                </tr>
              )}
            </tbody>
          </table>

          <h2>heroes ({heroes.length})</h2>
          <table className="table">
            <thead>
              <tr>
                <th>idx</th>
                <th>team</th>
                <th>name</th>
                <th>position</th>
              </tr>
            </thead>
            <tbody>
              {heroes.map((h) => (
                <tr key={h.index} className={h.isLocal ? 'me' : ''}>
                  <td className="mono">{h.index}</td>
                  <td className="mono">{h.team}</td>
                  <td>{h.name}</td>
                  <td className="mono">
                    {h.position
                      ? hex(h.position.offset) + ' ' + h.position.value.map((v) => num(v, 1)).join(', ')
                      : 'no position'}
                  </td>
                </tr>
              ))}
              {!heroes.length && (
                <tr>
                  <td colSpan="4" className="dim">none collected</td>
                </tr>
              )}
            </tbody>
          </table>

          <button className="btn" onClick={runScan} disabled={scan && scan.busy}>
            {scan && scan.busy ? 'scanning…' : 'scan object for position offsets'}
          </button>

          {scan && scan.rows && (
            <table className="table">
              <thead>
                <tr>
                  <th>offset</th>
                  <th>world</th>
                  <th>screen</th>
                  <th>off-centre px</th>
                </tr>
              </thead>
              <tbody>
                {scan.rows.map((r) => (
                  <tr key={r.offset}>
                    <td className="mono">{hex(r.offset)}</td>
                    <td className="mono">{r.value.map((v) => num(v, 1)).join(', ')}</td>
                    <td className="mono">{r.screen ? num(r.screen[0], 0) + ', ' + num(r.screen[1], 0) : '—'}</td>
                    <td className="mono">{num(r.offCentre, 0)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          {scan && scan.error && <p className="bad mono">{scan.error}</p>}
        </section>

        <section className="views">
          {conventions.map((conv) => (
            <figure key={conv.name} className="view">
              <figcaption>
                <span className="mono">{conv.name}</span>
                <span className="mono dim">
                  self {conv.self ? num(conv.self[0], 0) + ', ' + num(conv.self[1], 0) : 'off'}
                </span>
              </figcaption>
              <Screen conv={conv} client={client} />
            </figure>
          ))}
          {!conventions.length && <p className="dim">no camera read yet</p>}
        </section>
      </div>
    </div>
  )
}

createRoot(document.getElementById('root')).render(<App />)
