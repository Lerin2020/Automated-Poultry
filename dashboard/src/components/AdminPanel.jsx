import React, { useState, useEffect, useRef } from 'react';
import { Thermometer, Droplets, Wifi, Cpu, Clock, Database, Save, Check, Trash2, Download, Bell, AlertTriangle, ChevronDown, Activity, RefreshCw, Timer } from 'lucide-react';

const MAX_LOG = 100;
const LOG_KEY = 'poultry-event-log';
const HOURS = Array.from({ length: 24 }, (_, i) => i);
const fmtHr = h => `${h % 12 === 0 ? 12 : h % 12}:00 ${h >= 12 ? 'PM' : 'AM'}`;

const BADGE_MAP = {
  alerts: { label: 'ALERT', cls: 'badge badge-alert' },
  config: { label: 'CONFIG', cls: 'badge badge-config' },
  system: { label: 'SYSTEM', cls: 'badge badge-system' },
  feed:   { label: 'FEED',   cls: 'badge badge-feed' },
  egg:    { label: 'EGG',    cls: 'badge badge-egg' },
  waste:  { label: 'WASTE',  cls: 'badge badge-waste' },
};
function getBadge(topic) {
  for (const [k, v] of Object.entries(BADGE_MAP)) if (topic.includes(k)) return v;
  return { label: 'MSG', cls: 'badge badge-msg' };
}
function getRowCls(topic) {
  if (topic.includes('system/status')) return 'log-row log-row-heartbeat';
  if (topic.includes('alerts')) return 'log-row log-row-alert';
  if (topic.includes('config')) return 'log-row log-row-config';
  if (topic.includes('/data')) return 'log-row log-row-data';
  return 'log-row log-row-status';
}

function Gauge({ label, icon: Icon, value, max, unit, fillCls, danger }) {
  const pct = Math.min((value / max) * 100, 100);
  const bad = danger && value >= danger;
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <Icon size={14} className={bad ? 'anim-danger' : ''} style={{ color: bad ? '#ef4444' : '#94a3b8' }} />
          <span className="gauge-label">{label}</span>
        </div>
        <span className={bad ? 'gauge-value gauge-value-danger anim-danger' : 'gauge-value'}>{value}{unit}</span>
      </div>
      <div className="gauge-track">
        <div className={`gauge-fill anim-gauge ${bad ? 'gauge-fill-danger' : fillCls}`} style={{ width: `${pct}%` }} />
      </div>
    </div>
  );
}

function HourPicker({ value, onChange }) {
  const [open, setOpen] = useState(false);
  const ref = useRef(null);
  useEffect(() => {
    const h = e => { if (ref.current && !ref.current.contains(e.target)) setOpen(false); };
    document.addEventListener('mousedown', h);
    return () => document.removeEventListener('mousedown', h);
  }, []);
  return (
    <div style={{ position: 'relative' }} ref={ref}>
      <button onClick={() => setOpen(!open)} className="select-btn" style={{ padding: '8px 12px', borderRadius: 8, fontSize: 14 }}>
        {fmtHr(value)}
        <ChevronDown size={14} style={{ color: '#94a3b8', transition: 'transform .2s', transform: open ? 'rotate(180deg)' : '' }} />
      </button>
      {open && (
        <div className="dropdown anim-slide" style={{ borderRadius: 8, maxHeight: 192, overflowY: 'auto', zIndex: 30 }}>
          {HOURS.map(h => (
            <button key={h} onClick={() => { onChange(h); setOpen(false); }}
              className={`dropdown-item ${value === h ? 'dropdown-item-active' : ''}`} style={{ padding: '8px 12px', fontSize: 13 }}>
              {fmtHr(h)}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

export default function AdminPanel({ heartbeatData, publish, subscribe, onAnyMessage, isESPOnline, brokerHost }) {
  // ── RTC clock — parse ISO string with explicit local interpretation ──
  const [rtcTime, setRtcTime] = useState(null);
  const rtcBaseRef = useRef(null);
  const rtcReceivedAt = useRef(null);

  useEffect(() => {
    if (!heartbeatData.rtc) return;
    // ESP sends "2026-04-25T21:49:00" without timezone — parse manually as local time
    const parts = heartbeatData.rtc.match(/(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})/);
    if (parts) {
      const d = new Date(+parts[1], +parts[2] - 1, +parts[3], +parts[4], +parts[5], +parts[6]);
      if (!isNaN(d.getTime())) {
        rtcBaseRef.current = d;
        rtcReceivedAt.current = Date.now();
        setRtcTime(d); // show immediately, don't wait for interval
      }
    }
  }, [heartbeatData.rtc]);

  useEffect(() => {
    const timer = setInterval(() => {
      if (rtcBaseRef.current && rtcReceivedAt.current) {
        const elapsed = Date.now() - rtcReceivedAt.current;
        setRtcTime(new Date(rtcBaseRef.current.getTime() + elapsed));
      }
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  // ── Schedule ──
  const [schedFeed, setSchedFeed] = useState([7, 17]);
  const [schedEgg, setSchedEgg] = useState([8, 20]);
  const [schedWaste, setSchedWaste] = useState([6, 18]);
  const [eggThreshold, setEggThreshold] = useState(50);
  const [pushed, setPushed] = useState(false);
  const [rtcSynced, setRtcSynced] = useState(false);

  // ── Cycle Timings (in seconds) ──
  const [feedDistribute, setFeedDistribute] = useState(10);
  const [feedPause, setFeedPause] = useState(1);
  const [feedReverse, setFeedReverse] = useState(10);
  const [eggCollect, setEggCollect] = useState(10);
  const [wasteCycle, setWasteCycle] = useState(8);
  const [timingsPushed, setTimingsPushed] = useState(false);

  // ── Gantry speed (%) + auger run mode ──
  const [feedSpeed, setFeedSpeed] = useState(100);
  const [feedReturnSpeed, setFeedReturnSpeed] = useState(100);
  const [augerMode, setAugerMode] = useState(0);
  const AUGER_MODES = [
    { v: 0, label: 'Distribute only (default)' },
    { v: 1, label: 'Entire cycle' },
    { v: 2, label: 'While moving (out + back)' },
    { v: 3, label: 'Off (gantry only)' },
  ];

  const syncRTC = () => {
    const tzOffsetSec = new Date().getTimezoneOffset() * -60;
    const epoch = Math.floor(Date.now() / 1000) + tzOffsetSec;
    publish('poultry/config/cmd', JSON.stringify({ action: 'sync_rtc', epoch }));
    setRtcSynced(true);
    setTimeout(() => setRtcSynced(false), 2000);
  };

  useEffect(() => {
    const unsub = subscribe('poultry/config/status', msg => {
      try {
        const c = JSON.parse(msg.payloadString);
        if(c.feed) setSchedFeed(c.feed);
        if(c.egg) setSchedEgg(c.egg);
        if(c.waste) setSchedWaste(c.waste);
        if(c.egg_threshold!==undefined) setEggThreshold(c.egg_threshold);
        // Timings come as seconds from ESP32
        if(c.feed_distribute!==undefined) setFeedDistribute(c.feed_distribute);
        if(c.feed_pause!==undefined) setFeedPause(c.feed_pause);
        if(c.feed_reverse!==undefined) setFeedReverse(c.feed_reverse);
        if(c.egg_collect!==undefined) setEggCollect(c.egg_collect);
        if(c.waste_cycle!==undefined) setWasteCycle(c.waste_cycle);
        if(c.feed_speed!==undefined) setFeedSpeed(c.feed_speed);
        if(c.feed_return_speed!==undefined) setFeedReturnSpeed(c.feed_return_speed);
        if(c.auger_mode!==undefined) setAugerMode(c.auger_mode);
      } catch {}
    });
    publish('poultry/config/cmd', JSON.stringify({ action: 'get_config' }));
    return unsub;
  }, [subscribe, publish]);

  const pushConfig = () => {
    publish('poultry/config/cmd', JSON.stringify({ action:'update_schedule', feed:schedFeed, egg:schedEgg, waste:schedWaste, egg_threshold:eggThreshold }));
    setPushed(true); setTimeout(() => setPushed(false), 2000);
  };

  const pushTimings = () => {
    publish('poultry/config/cmd', JSON.stringify({
      action: 'update_timings',
      feed_distribute: feedDistribute,
      feed_pause: feedPause,
      feed_reverse: feedReverse,
      egg_collect: eggCollect,
      waste_cycle: wasteCycle,
      feed_speed: feedSpeed,
      feed_return_speed: feedReturnSpeed,
      auger_mode: augerMode
    }));
    setTimingsPushed(true); setTimeout(() => setTimingsPushed(false), 2000);
  };

  // ── Event Log ──
  const [log, setLog] = useState(() => { try { return JSON.parse(localStorage.getItem(LOG_KEY)) || []; } catch { return []; } });
  const logEnd = useRef(null);

  useEffect(() => {
    return onAnyMessage((topic, payload) => {
      if (topic === 'poultry/system/status') return;
      setLog(prev => {
        const updated = [...prev, { time: new Date().toLocaleTimeString(), date: new Date().toLocaleDateString(), topic, payload: payload.length > 120 ? payload.slice(0,120)+'...' : payload }].slice(-MAX_LOG);
        localStorage.setItem(LOG_KEY, JSON.stringify(updated));
        return updated;
      });
    });
  }, [onAnyMessage]);

  useEffect(() => { logEnd.current?.scrollIntoView({ behavior: 'smooth' }); }, [log]);

  const exportCSV = () => {
    const csv = 'Date,Time,Topic,Payload\n' + log.map(e => `${e.date},${e.time},"${e.topic}","${e.payload.replace(/"/g,'""')}"`).join('\n');
    const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([csv],{type:'text/csv'}));
    a.download = `poultry-events-${new Date().toISOString().split('T')[0]}.csv`; a.click();
  };

  // ── Alerts ──
  const [alerts, setAlerts] = useState([]);
  useEffect(() => {
    return subscribe('poultry/alerts', msg => {
      try { const a = JSON.parse(msg.payloadString); setAlerts(prev => [{ ...a, id: Date.now(), time: new Date().toLocaleTimeString(), date: new Date().toLocaleDateString() }, ...prev].slice(0,20)); } catch {}
    });
  }, [subscribe]);

  return (
    <div className="grid-admin anim-fade">

      {/* ═══ System Health ═══ */}
      <div className="card">
        <div className="section-header">
          <div className="icon-box-sm" style={{ background: '#ecfdf5' }}><Activity size={16} style={{ color: '#10b981' }} /></div>
          System Health
          <div style={{ marginLeft: 'auto' }} className={isESPOnline ? 'badge badge-feed' : 'badge badge-alert'}>{isESPOnline ? 'LIVE' : 'OFFLINE'}</div>
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
          <Gauge label="Temperature" icon={Thermometer} value={heartbeatData.temp} max={50} unit="°C" fillCls="gauge-fill-temp" danger={40} />
          <Gauge label="Humidity" icon={Droplets} value={heartbeatData.humidity} max={100} unit="%" fillCls="gauge-fill-humidity" />
          <Gauge label="WiFi Signal" icon={Wifi} value={Math.min(Math.max(heartbeatData.rssi+100,0),100)} max={100} unit={` (${heartbeatData.rssi} dBm)`} fillCls="gauge-fill-wifi" />
          <Gauge label="Free Heap" icon={Cpu} value={Math.round(heartbeatData.heap/1024)} max={320} unit=" KB" fillCls="gauge-fill-heap" />
        </div>

        {/* RTC */}
        <div className="divider" style={{ marginTop: 24, paddingTop: 16 }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <Clock size={14} style={{ color: '#94a3b8' }} />
              <span className="gauge-label">RTC Clock</span>
              <button onClick={syncRTC} disabled={!isESPOnline} className="btn-sm" style={{ marginLeft: 8, display: 'flex', alignItems: 'center', gap: 4 }}>
                {rtcSynced ? <><Check size={11} className="anim-check" /> Synced!</> : <><RefreshCw size={11} /> Sync</>}
              </button>
            </div>
            <div style={{ textAlign: 'right' }}>
              {rtcTime ? (<>
                <p className="rtc-time">{rtcTime.toLocaleTimeString()}</p>
                <p className="rtc-date">{rtcTime.toLocaleDateString('en-US',{weekday:'short',month:'short',day:'numeric',year:'numeric'})}</p>
              </>) : (
                <p className="rtc-waiting">{heartbeatData.rtc ? `Raw: ${heartbeatData.rtc}` : 'Waiting for heartbeat...'}</p>
              )}
            </div>
          </div>
        </div>

        {/* Queue */}
        <div className="divider" style={{ marginTop: 16, paddingTop: 16 }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <Database size={14} style={{ color: '#94a3b8' }} />
              <span className="gauge-label">Offline Queue</span>
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
              <span className="gauge-value">{heartbeatData.queue} items</span>
              <button onClick={() => publish('poultry/config/cmd',JSON.stringify({action:'clear_queue'}))} disabled={heartbeatData.queue===0} className="btn-sm btn-sm-danger">Clear</button>
            </div>
          </div>
        </div>

        {!heartbeatData.dht_ok && isESPOnline && (
          <div className="dht-warn"><AlertTriangle size={13} /> DHT22 sensor read failed — check wiring</div>
        )}
      </div>

      {/* ═══ Schedule Config ═══ */}
      <div className="card">
        <div className="section-header">
          <div className="icon-box-sm" style={{ background: 'rgba(99,102,241,.1)' }}><Clock size={16} style={{ color: '#6366f1' }} /></div>
          Schedule Configuration
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 20 }}>
          <div><label className="label">Feeding Times</label><div className="grid-cards"  style={{ gridTemplateColumns: '1fr 1fr' }}><HourPicker value={schedFeed[0]} onChange={h=>setSchedFeed([h,schedFeed[1]])} /><HourPicker value={schedFeed[1]} onChange={h=>setSchedFeed([schedFeed[0],h])} /></div></div>
          <div><label className="label">Egg Collection Times</label><div className="grid-cards" style={{ gridTemplateColumns: '1fr 1fr' }}><HourPicker value={schedEgg[0]} onChange={h=>setSchedEgg([h,schedEgg[1]])} /><HourPicker value={schedEgg[1]} onChange={h=>setSchedEgg([schedEgg[0],h])} /></div></div>
          <div><label className="label">Waste Flush Times</label><div className="grid-cards" style={{ gridTemplateColumns: '1fr 1fr' }}><HourPicker value={schedWaste[0]} onChange={h=>setSchedWaste([h,schedWaste[1]])} /><HourPicker value={schedWaste[1]} onChange={h=>setSchedWaste([schedWaste[0],h])} /></div></div>
          <div><label className="label">Egg Alert Threshold: <span style={{ color: '#6366f1' }}>{eggThreshold}</span></label><input type="range" min="10" max="200" value={eggThreshold} onChange={e=>setEggThreshold(+e.target.value)} className="range" /><div className="range-labels"><span>10</span><span>200</span></div></div>
          <button onClick={pushConfig} disabled={!isESPOnline} className="btn-primary" style={{ width: '100%', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8 }}>
            {pushed ? <><Check size={16} className="anim-check" /> Config Pushed!</> : <><Save size={16} /> Push to ESP32</>}
          </button>
        </div>
      </div>

      {/* ═══ Cycle Timings ═══ */}
      <div className="card">
        <div className="section-header">
          <div className="icon-box-sm" style={{ background: 'rgba(245,158,11,.1)' }}><Timer size={16} style={{ color: '#f59e0b' }} /></div>
          Cycle Timings
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
          <div style={{ borderBottom: '1px solid rgba(148,163,184,.15)', paddingBottom: 12 }}>
            <label className="label" style={{ marginBottom: 8, display: 'block', color: '#6366f1', fontWeight: 600, fontSize: 12, textTransform: 'uppercase', letterSpacing: '.5px' }}>Feeding Cycle</label>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span className="gauge-label">Distribute (auger + gantry)</span>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <input type="number" min={1} max={60} value={feedDistribute} onChange={e => setFeedDistribute(Math.max(1,+e.target.value))} className="input-num" />
                  <span className="gauge-label">sec</span>
                </div>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span className="gauge-label">Pause at end</span>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <input type="number" min={0} max={10} value={feedPause} onChange={e => setFeedPause(Math.max(0,+e.target.value))} className="input-num" />
                  <span className="gauge-label">sec</span>
                </div>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span className="gauge-label">Return home</span>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <input type="number" min={1} max={60} value={feedReverse} onChange={e => setFeedReverse(Math.max(1,+e.target.value))} className="input-num" />
                  <span className="gauge-label">sec</span>
                </div>
              </div>

              <div>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <span className="gauge-label">Distribute speed</span>
                  <span className="gauge-value" style={{ color: '#6366f1' }}>{feedSpeed}%</span>
                </div>
                <input type="range" min={0} max={100} value={feedSpeed} onChange={e => setFeedSpeed(+e.target.value)} className="range" />
              </div>
              <div>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <span className="gauge-label">Return speed</span>
                  <span className="gauge-value" style={{ color: '#6366f1' }}>{feedReturnSpeed}%</span>
                </div>
                <input type="range" min={0} max={100} value={feedReturnSpeed} onChange={e => setFeedReturnSpeed(+e.target.value)} className="range" />
              </div>
              <div>
                <label className="label" style={{ marginBottom: 6, display: 'block' }}>Auger runs during</label>
                <select value={augerMode} onChange={e => setAugerMode(+e.target.value)} className="select-btn" style={{ width: '100%', padding: '8px 12px', borderRadius: 8, fontSize: 13 }}>
                  {AUGER_MODES.map(m => <option key={m.v} value={m.v}>{m.label}</option>)}
                </select>
              </div>
            </div>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <span className="gauge-label">Egg Collection</span>
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <input type="number" min={1} max={60} value={eggCollect} onChange={e => setEggCollect(Math.max(1,+e.target.value))} className="input-num" />
              <span className="gauge-label">sec</span>
            </div>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <span className="gauge-label">Waste Cycle</span>
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <input type="number" min={1} max={60} value={wasteCycle} onChange={e => setWasteCycle(Math.max(1,+e.target.value))} className="input-num" />
              <span className="gauge-label">sec</span>
            </div>
          </div>
          <button onClick={pushTimings} disabled={!isESPOnline} className="btn-primary" style={{ width: '100%', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8 }}>
            {timingsPushed ? <><Check size={16} className="anim-check" /> Timings Pushed!</> : <><Save size={16} /> Push Timings</>}
          </button>
        </div>
      </div>

      {/* ═══ Event Log ═══ */}
      <div className="card col-full">
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
          <div className="section-header" style={{ marginBottom: 0 }}>
            <div className="icon-box-sm" style={{ background: '#eff6ff' }}><Database size={16} style={{ color: '#3b82f6' }} /></div>
            Event Log
            <span className="badge badge-system" style={{ marginLeft: 4 }}>{log.length}</span>
          </div>
          <div style={{ display: 'flex', gap: 8 }}>
            <a href={`http://${brokerHost}/log`} download="poultry_activity_log.json" className={`btn-sm${!isESPOnline ? ' btn-sm-disabled' : ''}`} style={{ textDecoration: 'none', pointerEvents: isESPOnline ? 'auto' : 'none', opacity: isESPOnline ? 1 : 0.4 }}><Download size={12} /> Activity Log</a>
            <button onClick={exportCSV} disabled={!log.length} className="btn-sm"><Download size={12} /> Export CSV</button>
            <button onClick={()=>{setLog([]);localStorage.removeItem(LOG_KEY);}} disabled={!log.length} className="btn-sm btn-sm-danger"><Trash2 size={12} /> Clear</button>
          </div>
        </div>
        <div className="log-wrap"><div className="log-scroll">
          <table className="log-table"><thead className="log-thead"><tr><th className="log-th">Time</th><th className="log-th">Topic</th><th className="log-th">Payload</th></tr></thead>
          <tbody>
            {!log.length ? <tr><td colSpan={3} className="log-empty">No events captured yet.</td></tr> :
              log.map((e,i) => { const b = getBadge(e.topic); return (
                <tr key={i} className={getRowCls(e.topic)}><td className="log-td log-td-time">{e.time}</td><td className="log-td"><span className={b.cls}>{b.label}</span></td><td className="log-td log-td-payload">{e.payload}</td></tr>
              );})
            }
            <tr ref={logEnd}></tr>
          </tbody></table>
        </div></div>
      </div>

      {/* ═══ Alerts ═══ */}
      <div className="card col-full">
        <div className="section-header" style={{ marginBottom: 16 }}>
          <div className="icon-box-sm" style={{ background: '#fffbeb' }}><Bell size={16} style={{ color: '#f59e0b' }} /></div>
          Alerts
          {alerts.length > 0 && <span className="alert-count">{alerts.length}</span>}
        </div>
        {!alerts.length ? (
          <div className="alert-empty"><Bell size={24} style={{ margin: '0 auto 8px', color: '#cbd5e1' }} /><p className="alert-empty-text">No alerts yet.</p></div>
        ) : (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 12, maxHeight: 256, overflowY: 'auto' }}>
            {alerts.map(a => (
              <div key={a.id} className={`alert-card anim-fade ${a.type==='temperature' ? 'alert-temp' : 'alert-warn'}`}>
                <div className={a.type==='temperature' ? 'alert-icon-temp' : 'alert-icon-warn'}>
                  {a.type==='temperature' ? <Thermometer size={14} style={{color:'#ef4444'}} /> : <AlertTriangle size={14} style={{color:'#f59e0b'}} />}
                </div>
                <div style={{ flex: 1, minWidth: 0 }}>
                  <p className={a.type==='temperature' ? 'alert-msg-temp' : 'alert-msg-warn'}>{a.message}</p>
                  <p className="alert-time">{a.date} at {a.time}</p>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
