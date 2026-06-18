import React, { useState, useEffect, useRef } from 'react';
import { useMQTT } from './hooks/useMQTT';
import { Moon, Sun, Settings, Utensils, Egg, Trash2, Square, Loader2, Activity, ChevronDown, Zap, Clock, BarChart3, Shield, Thermometer, Droplets, Wifi, AlertTriangle, Link2 } from 'lucide-react';
import AdminPanel from './components/AdminPanel';

function App() {
  const { isBrokerConnected, isESPOnline, heartbeatData, publish, subscribe, onAnyMessage, brokerHost, changeBrokerHost } = useMQTT();
  const [activeTab, setActiveTab] = useState('controls');
  const [birds, setBirds] = useState(10);
  const [ageRange, setAgeRange] = useState("2");
  const [isDropdownOpen, setIsDropdownOpen] = useState(false);
  const dropdownRef = useRef(null);
  const [alertCount, setAlertCount] = useState(0);
  const [showConnModal, setShowConnModal] = useState(false);
  const [tempHost, setTempHost] = useState(brokerHost);
  
  const growthOptions = { "0":"Pullet (12-16 wks)", "1":"Pre-lay (17-20 wks)", "2":"Active Layer (21-30 wks)", "3":"Late Layer (31-52+ wks)" };
  const [feedState, setFeedState] = useState({ state: 'idle' });
  const [eggState, setEggState] = useState({ state: 'idle', eggs_l1: 0, eggs_l2: 0, total: 0 });
  const [wasteState, setWasteState] = useState({ state: 'idle' });
  const [schedule, setSchedule] = useState({ feed: [[7, 0], [17, 0]], egg: [[8, 0], [20, 0]], waste: [[6, 0], [18, 0]] });
  const prevEggTotal = useRef(0);
  const [eggAnimKey, setEggAnimKey] = useState(0);

  // Track previous cycle states to detect idle transitions outside of state updaters
  const prevFeedState  = useRef('idle');
  const prevEggCycleState  = useRef('idle');
  const prevWasteState = useRef('idle');

  const [toast, setToast] = useState(null);
  const toastTimer = useRef(null);
  const showToast = (msg) => {
    clearTimeout(toastTimer.current);
    setToast(msg);
    toastTimer.current = setTimeout(() => setToast(null), 3000);
  };

  const [isDarkMode, setIsDarkMode] = useState(() => { const s = localStorage.getItem('poultry-dark-mode'); return s !== null ? s === 'true' : true; });
  useEffect(() => { document.documentElement.classList.toggle('dark', isDarkMode); localStorage.setItem('poultry-dark-mode', isDarkMode); }, [isDarkMode]);

  const feedPerBird = [60, 85, 105, 110];
  const birdCount = Math.max(0, parseInt(birds, 10) || 0);
  const dailyGrams = (birdCount * feedPerBird[parseInt(ageRange)]) / 1000;
  const perCycleGrams = dailyGrams / 2;

  useEffect(() => {
    const h = e => { if (dropdownRef.current && !dropdownRef.current.contains(e.target)) setIsDropdownOpen(false); };
    document.addEventListener('mousedown', h); return () => document.removeEventListener('mousedown', h);
  }, []);

  useEffect(() => {
    const u = [
      subscribe('poultry/feed/status', m => {
        const d = JSON.parse(m.payloadString);
        if (prevFeedState.current !== 'idle' && d.state === 'idle') showToast('Feeding cycle complete');
        if (d.state) prevFeedState.current = d.state;
        setFeedState(p => ({...p, ...d}));
      }),
      subscribe('poultry/egg/status', m => {
        const d = JSON.parse(m.payloadString);
        if (prevEggCycleState.current === 'collecting' && d.state === 'idle') showToast(`Egg collection complete — ${d.total ?? 0} eggs`);
        if (d.state) prevEggCycleState.current = d.state;
        setEggState(p => ({...p, ...d}));
      }),
      subscribe('poultry/egg/data', m => { const d = JSON.parse(m.payloadString); setEggState(p => ({...p,...d})); if (d.total !== undefined && d.total !== prevEggTotal.current) { prevEggTotal.current = d.total; setEggAnimKey(k => k+1); } }),
      subscribe('poultry/waste/status', m => {
        const d = JSON.parse(m.payloadString);
        if (prevWasteState.current !== 'idle' && d.state === 'idle') showToast('Waste flush complete');
        if (d.state) prevWasteState.current = d.state;
        setWasteState(p => ({...p, ...d}));
      }),
      subscribe('poultry/alerts', () => { if (activeTab !== 'admin') setAlertCount(c => c+1); }),
      subscribe('poultry/config/status', m => {
        const c = JSON.parse(m.payloadString);
        const norm = arr => Array.isArray(arr) ? arr.map(s => Array.isArray(s) ? [s[0], s[1] ?? 0] : [s, 0]) : null;
        setSchedule(p => ({ feed: norm(c.feed) || p.feed, egg: norm(c.egg) || p.egg, waste: norm(c.waste) || p.waste }));
      }),
    ];
    return () => u.forEach(fn => fn());
  }, [subscribe, activeTab]);

  // Fetch the real schedule so "Next:" labels reflect Admin changes
  useEffect(() => {
    if (isBrokerConnected) publish('poultry/config/cmd', JSON.stringify({ action: 'get_config' }));
  }, [isBrokerConnected, publish]);

  const handleFeed = () => publish('poultry/feed/cmd', JSON.stringify({ action: 'start' }));
  const handleStopFeed = () => publish('poultry/feed/cmd', JSON.stringify({ action: 'stop' }));
  const handleCollectEgg = () => publish('poultry/egg/cmd', JSON.stringify({ action: 'start' }));
  const handleStopEgg = () => publish('poultry/egg/cmd', JSON.stringify({ action: 'stop' }));
  const handleWaste = () => publish('poultry/waste/cmd', JSON.stringify({ action: 'start' }));
  const handleStopWaste = () => publish('poultry/waste/cmd', JSON.stringify({ action: 'stop' }));

  const getNext = (slots) => {
    if (!Array.isArray(slots) || !slots.length) return '--';
    const now = new Date();
    const nowMin = now.getHours() * 60 + now.getMinutes();
    const mins = slots.map(([h, m]) => h * 60 + (m || 0)).sort((a, b) => a - b);
    let n = mins.find(m => m > nowMin), tom = false;
    if (n === undefined) { n = mins[0]; tom = true; }
    const h = Math.floor(n / 60), m = n % 60;
    return `${h%12===0?12:h%12}:${String(m).padStart(2,'0')} ${h>=12?'PM':'AM'}${tom?' (Tomorrow)':''}`;
  };

  const LiveBadge = () => <span className="live-badge anim-ring"><Activity size={13} strokeWidth={2.5} /> LIVE</span>;

  return (
    <div className="page">
      {toast && (
        <div style={{ position: 'fixed', bottom: 24, left: '50%', transform: 'translateX(-50%)', zIndex: 100, background: '#22c55e', color: '#fff', padding: '10px 20px', borderRadius: 12, fontWeight: 600, fontSize: 14, boxShadow: '0 4px 16px rgba(0,0,0,.25)', pointerEvents: 'none' }} className="anim-fade">
          {toast}
        </div>
      )}
      {/* ══ HEADER ══ */}
      <header className="header anim-fade">
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          <div className="header-logo"><Zap size={22} strokeWidth={2.5} /></div>
          <div>
            <h1 className="header-title">PoultryMG</h1>
            <p className="header-sub">Automation Dashboard</p>
          </div>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          <div className="tab-bar hidden-mobile">
            <button onClick={() => setActiveTab('controls')} className={`tab-btn ${activeTab==='controls'?'tab-active':'tab-inactive'}`}><Zap size={14} style={{display:'inline',marginRight:6,verticalAlign:'-2px'}} />Controls</button>
            <button onClick={() => {setActiveTab('admin');setAlertCount(0);}} className={`tab-btn ${activeTab==='admin'?'tab-active':'tab-inactive'}`}><Shield size={14} style={{display:'inline',marginRight:6,verticalAlign:'-2px'}} />Admin{alertCount>0&&<span className="tab-badge">{alertCount}</span>}</button>
          </div>
          <button onClick={() => setShowConnModal(true)} style={{ padding: 8, borderRadius: 12, cursor: 'pointer', color: '#94a3b8', background: 'transparent', border: 'none' }} title={`Broker: ${brokerHost}`}>
            <Link2 size={18} />
          </button>
          <button onClick={() => setIsDarkMode(!isDarkMode)} style={{ padding: 8, borderRadius: 12, cursor: 'pointer', color: '#94a3b8', background: 'transparent', border: 'none' }}>
            {isDarkMode ? <Sun size={18} /> : <Moon size={18} />}
          </button>
          <div className={isESPOnline ? 'badge-online' : 'badge-offline'}>
            <div className={isESPOnline ? 'dot-online' : 'dot-offline'}></div>
            {isESPOnline ? 'ESP32 Online' : isBrokerConnected ? 'Hardware Offline' : 'Connecting...'}
          </div>
        </div>
      </header>

      {/* Connection Settings Modal */}
      {showConnModal && (
        <div style={{ position: 'fixed', inset: 0, zIndex: 50, display: 'flex', alignItems: 'center', justifyContent: 'center', background: 'rgba(0,0,0,.5)', backdropFilter: 'blur(4px)' }} onClick={() => setShowConnModal(false)}>
          <div className="card anim-fade" style={{ maxWidth: 400, width: '90%' }} onClick={e => e.stopPropagation()}>
            <div className="section-header"><Link2 size={16} style={{ color: '#6366f1' }} /> ESP32 Connection</div>
            <p style={{ fontSize: 13, color: '#64748b', marginBottom: 12 }}>Enter the ESP32's IP address if <strong>poultry.local</strong> doesn't work on your device (e.g. Android 7).</p>
            <label className="label">Broker Host</label>
            <input className="input" value={tempHost} onChange={e => setTempHost(e.target.value)} placeholder="poultry.local or 192.168.1.x" />
            <div style={{ display: 'flex', gap: 8, marginTop: 16 }}>
              <button className="btn-primary" style={{ flex: 1 }} onClick={() => { changeBrokerHost(tempHost); setShowConnModal(false); }}>Connect</button>
              <button className="btn-stop" onClick={() => { setTempHost('poultry.local'); changeBrokerHost('poultry.local'); setShowConnModal(false); }}>Reset to mDNS</button>
            </div>
            <p style={{ fontSize: 11, color: '#94a3b8', marginTop: 12 }}>Current: <strong>{brokerHost}</strong> — {isBrokerConnected ? '✅ Connected' : '❌ Disconnected'}</p>
          </div>
        </div>
      )}

      {/* Mobile tabs */}
      <div className="tab-bar visible-mobile" style={{ marginBottom: 24 }}>
        <button onClick={() => setActiveTab('controls')} className={`tab-btn ${activeTab==='controls'?'tab-active':'tab-inactive'}`} style={{flex:1}}>Controls</button>
        <button onClick={() => {setActiveTab('admin');setAlertCount(0);}} className={`tab-btn ${activeTab==='admin'?'tab-active':'tab-inactive'}`} style={{flex:1}}>Admin{alertCount>0&&<span className="tab-badge">{alertCount}</span>}</button>
      </div>

      {/* ══ CONTROLS ══ */}
      {activeTab === 'controls' && (
        <>
        {/* Environment Strip */}
        <div className="env-strip anim-fade">
          <div className="env-item">
            <Thermometer size={14} className={heartbeatData.temp >= 40 ? 'anim-danger' : ''} style={{ color: heartbeatData.temp >= 40 ? '#ef4444' : '#f59e0b' }} />
            <span className="env-label">Temp</span>
            <span className={`env-value ${heartbeatData.temp >= 40 ? 'env-value-danger' : ''}`}>{heartbeatData.dht_ok ? `${heartbeatData.temp}°C` : '--'}</span>
          </div>
          <div className="env-divider"></div>
          <div className="env-item">
            <Droplets size={14} style={{ color: '#3b82f6' }} />
            <span className="env-label">Humidity</span>
            <span className="env-value">{heartbeatData.dht_ok ? `${heartbeatData.humidity}%` : '--'}</span>
          </div>
          <div className="env-divider"></div>
          <div className="env-item">
            <Wifi size={14} style={{ color: '#6366f1' }} />
            <span className="env-label">WiFi</span>
            <span className="env-value">{heartbeatData.rssi} dBm</span>
          </div>
          {heartbeatData.temp >= 40 && (
            <>
              <div className="env-divider"></div>
              <div className="env-item env-danger-msg anim-danger">
                <AlertTriangle size={14} />
                <span>HIGH TEMP!</span>
              </div>
            </>
          )}
        </div>

        <div className="grid-main">
          {/* Settings sidebar */}

          <div className="col-side anim-fade" style={{ animationDelay: '.1s' }}>
            <div className="card" style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
              <div className="section-header">
                <div className="icon-box-sm"><Settings size={16} style={{ color: '#64748b' }} /></div>
                Farm Settings
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 20, flex: 1 }}>
                <div>
                  <label className="label">Bird Count</label>
                  <div style={{ position: 'relative' }}>
                    <input type="number" value={birds} onChange={e => setBirds(e.target.value)} className="input" />
                    <span style={{ position: 'absolute', right: 16, top: 14, fontSize: 12, color: '#94a3b8', fontWeight: 600 }}>birds</span>
                  </div>
                </div>
                <div>
                  <label className="label">Growth Stage</label>
                  <div style={{ position: 'relative' }} ref={dropdownRef}>
                    <button onClick={() => setIsDropdownOpen(!isDropdownOpen)} className="select-btn">
                      {growthOptions[ageRange]}
                      <ChevronDown size={16} style={{ color: '#94a3b8', transition: 'transform .2s', transform: isDropdownOpen ? 'rotate(180deg)' : '' }} />
                    </button>
                    {isDropdownOpen && (
                      <div className="dropdown anim-slide">
                        {Object.entries(growthOptions).map(([v, l]) => (
                          <button key={v} onClick={() => {setAgeRange(v);setIsDropdownOpen(false);}} className={`dropdown-item ${ageRange===v?'dropdown-item-active':''}`}>{l}</button>
                        ))}
                      </div>
                    )}
                  </div>
                </div>
                <div style={{ paddingTop: 8, marginTop: 'auto' }}>
                  <div className="summary-box">
                    <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 4 }}>
                      <BarChart3 size={14} style={{ color: '#6366f1' }} />
                      <span className="label" style={{ margin: 0 }}>Feed Summary</span>
                    </div>
                    <div className="summary-row"><span className="summary-label">Daily Feed</span><span className="summary-value">{dailyGrams.toFixed(2)} kg</span></div>
                    <div className="summary-divider"></div>
                    <div className="summary-row"><span className="summary-label">Per Cycle (2x)</span><span className="summary-value-accent">{perCycleGrams.toFixed(2)} kg</span></div>
                  </div>
                </div>
              </div>
            </div>
          </div>

          {/* Main cards */}
          <div className="col-main">
            <div className="grid-cards">
              {/* Feed */}
              {(() => { const feedActive = feedState.state && feedState.state !== 'idle'; return (
              <div className={`card anim-fade ${feedActive?'card-active':''}`} style={{ animationDelay: '.15s', display: 'flex', flexDirection: 'column' }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', marginBottom: 24 }}>
                  <div style={{ display: 'flex', gap: 14, alignItems: 'flex-start' }}>
                    <div className={feedActive?'icon-box icon-box-active':'icon-box'}>
                      {feedActive ? <Loader2 size={22} className="animate-spin" /> : <Utensils size={22} />}
                    </div>
                    <div>
                      <h2 style={{ fontWeight: 700, fontSize: 15 }}>{feedActive ? `Feed: ${feedState.state.charAt(0).toUpperCase()+feedState.state.slice(1)}...` : 'Feeding System'}</h2>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginTop: 2 }}>
                        <Clock size={12} style={{ color: '#94a3b8' }} />
                        <span className="next-label">Next: <span className="next-value">{getNext(schedule.feed)}</span></span>
                      </div>
                    </div>
                  </div>
                  {feedActive && <LiveBadge />}
                </div>
                <div style={{ marginBottom: 'auto' }}>
                  <span className="gauge-label">Gantry Progress</span>
                  <div className="progress-track" style={{ marginTop: 8 }}>
                    <div className="progress-fill" style={{ width: feedActive?'100%':'0%' }}></div>
                  </div>
                  <div style={{ marginTop: 8, textAlign: 'right' }}><span className="progress-label">Target: {perCycleGrams.toFixed(2)}kg per cycle</span></div>
                </div>
                <div style={{ display: 'flex', gap: 12, marginTop: 24 }}>
                  <button onClick={handleFeed} disabled={feedActive || !isBrokerConnected} className="btn-primary" style={{ flex: 1 }}>Force Feed</button>
                  <button onClick={handleStopFeed} disabled={!isBrokerConnected} className="btn-stop">Stop</button>
                </div>
              </div>
              ); })()}

              {/* Egg */}
              <div className={`card anim-fade ${eggState.state==='collecting'?'card-active':''}`} style={{ animationDelay: '.2s', display: 'flex', flexDirection: 'column' }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', marginBottom: 24 }}>
                  <div style={{ display: 'flex', gap: 14, alignItems: 'flex-start' }}>
                    <div className={eggState.state==='collecting'?'icon-box icon-box-active':'icon-box'}>
                      {eggState.state==='collecting' ? <Loader2 size={22} className="animate-spin" /> : <Egg size={22} />}
                    </div>
                    <div>
                      <h2 style={{ fontWeight: 700, fontSize: 15 }}>{eggState.state==='collecting'?'Scanning Belt...':'Egg Collection'}</h2>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginTop: 2 }}>
                        <Clock size={12} style={{ color: '#94a3b8' }} />
                        <span className="next-label">Next: <span className="next-value">{getNext(schedule.egg)}</span></span>
                      </div>
                    </div>
                  </div>
                  {eggState.state==='collecting' && <LiveBadge />}
                </div>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, marginBottom: 'auto' }}>
                  <div className="stat-box"><p className="stat-label">Layer 1</p><p key={`l1-${eggAnimKey}`} className="stat-value anim-pop">{eggState.eggs_l1}</p></div>
                  <div className="stat-box"><p className="stat-label">Layer 2</p><p key={`l2-${eggAnimKey}`} className="stat-value anim-pop">{eggState.eggs_l2}</p></div>
                  <div className="stat-total"><span className="stat-total-label">Total Collected</span><span key={`t-${eggAnimKey}`} className="stat-total-value anim-pop">{eggState.total}</span></div>
                </div>
                <div style={{ display: 'flex', gap: 12, marginTop: 24 }}>
                  <button onClick={handleCollectEgg} disabled={eggState.state==='collecting' || !isBrokerConnected} className="btn-primary" style={{ flex: 1 }}>Force Collect</button>
                  <button onClick={handleStopEgg} disabled={!isBrokerConnected} className="btn-stop">Stop</button>
                </div>
              </div>
            </div>

            {/* Waste */}
            <div className={`card anim-fade ${wasteState.state==='active'?'card-active':''}`} style={{ animationDelay: '.25s', display: 'flex', flexWrap: 'wrap', gap: 16, alignItems: 'center', justifyContent: 'space-between' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
                <div className={wasteState.state==='active'?'icon-box icon-box-active':'icon-box'}>
                  {wasteState.state==='active' ? <Loader2 size={22} className="animate-spin" /> : <Trash2 size={22} />}
                </div>
                <div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                    <h2 style={{ fontWeight: 700, fontSize: 15 }}>{wasteState.state==='active'?'Flushing Waste...':'Waste Management'}</h2>
                    {wasteState.state==='active' && <LiveBadge />}
                  </div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginTop: 4 }}>
                    <Clock size={12} style={{ color: '#94a3b8' }} />
                    <span className="next-label">Next: <span className="next-value">{getNext(schedule.waste)}</span></span>
                  </div>
                </div>
              </div>
              <div style={{ display: 'flex', gap: 12, marginLeft: 'auto' }}>
                <button onClick={handleWaste} disabled={wasteState.state==='active' || !isBrokerConnected} className="btn-primary" style={{ padding: '10px 20px' }}>Force Flush</button>
                <button onClick={handleStopWaste} disabled={!isBrokerConnected} className="btn-stop" style={{ padding: '10px 16px' }}><Square size={15} fill="currentColor" /></button>
              </div>
            </div>
          </div>
        </div>
        </>
      )}

      {/* ══ ADMIN ══ */}
      {activeTab === 'admin' && <AdminPanel heartbeatData={heartbeatData} publish={publish} subscribe={subscribe} onAnyMessage={onAnyMessage} isESPOnline={isESPOnline} brokerHost={brokerHost} />}


      <footer className="footer">Automated Poultry Farm Management System</footer>
    </div>
  );
}

export default App;