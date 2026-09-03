// ==========================================
// CHASSIS DRIVE ASSEMBLY VISUALIZER ENGINE
// ==========================================

// Global state
const state = {
  currentDesign: "B", // "A" = Dual-Stage Belts, "B" = Worm Gearmotor Direct Drive
  hoveredComponent: null,
  showDimensions: true,
  showClearances: true,
  showBelts: true,
  showLabels: true,
  motorRpm: 6000,
  wormRatio: 31,
  surface: "concrete",
  designAMotor: "550",
  designAMount: "inboard",
  designAStages: 1,
};

// Component Database (Holds specifications for both designs)
const COMPONENTS = {
  chassis_box: {
    id: "chassis_box",
    name: "Internal Foot Cavity Envelope",
    category: "chassis",
    coords: "X: 0 to 215 mm | Y: 0 to 100 mm | Z: 0 to 85 mm",
    zSpan: "0.0 to 85.0 mm",
    dims: "215 mm L × 100 mm H × 85 mm W",
    spec: "Internal reference datum envelope",
    desc: "The design space limit of the foot cavity. All X coordinates are relative to the front inner wall (X=0). Y=0 is the floor contact plane (bottom of the wheels). Z=0 is the outer chassis plate reference surface.",
  },
  outer_plate: {
    id: "outer_plate",
    name: "Outer Baltic Birch Plate (12mm)",
    category: "chassis",
    coords: "X: 2.5 to 212.5 mm | Y: 0 to 95 mm",
    zSpan: "0.0 to 12.0 mm",
    dims: "210 mm L × 95 mm H × 12 mm Thick",
    spec: "Baltic Birch Plywood",
    machining: "Drilled with 22mm pockets (7mm deep) & 10mm through holes",
    desc: "The structural side plate on the outboard side. Houses bearings for the front axle, rear drive axle, and jackshaft (Design A only), pressed from the inside face.",
  },
  inner_plate: {
    id: "inner_plate",
    name: "Inner Baltic Birch Plate (12mm)",
    category: "chassis",
    coords: "X: 2.5 to 212.5 mm | Y: 0 to 95 mm",
    zSpan: "70.0 to 82.0 mm (Design A) | 65.0 to 77.0 mm (Design B)",
    dims: "210 mm L × 95 mm H × 12 mm Thick",
    spec: "Baltic Birch Plywood",
    machining: "Bored with bearing pockets, motor pilot, and mounting holes",
    desc: "The structural side plate on the inboard side. In Design B, it mounts at Z=65..77mm directly next to the 27mm gearbox housing, leaving an 8mm wall clearance margin.",
  },
  front_wheel: {
    id: "front_wheel",
    name: "Front Idler Wheel Assembly",
    category: "wheel",
    coords: "Center: (40.0, 30.0) mm",
    zSpan: "14.0 to 38.0 mm",
    dims: "Ø 60 mm × 24 mm Wide",
    spec: "Standard inline skate wheel (polyurethane)",
    machining: "Houses two 608RS bearings on an 8mm precision steel axle",
    desc: "Runs freely on an 8mm steel rod. Positioned at X=40, Y=30, giving a profile span of X=10 to 70 and Y=0 to 60. Provides front stability.",
  },
  rear_wheel: {
    id: "rear_wheel",
    name: "Rear Drive Wheel",
    category: "wheel",
    coords: "Center: (175.0, 30.0) mm",
    zSpan: "14.0 to 38.0 mm",
    dims: "Ø 60 mm × 24 mm Wide",
    spec: "Standard inline skate wheel (polyurethane)",
    machining: "Three M4 bolts (Design A) or coupled directly to worm shaft (Design B)",
    desc: "The primary drive wheel. In Design A, it is driven by a pulley; in Design B, it is driven directly by the worm gearbox output shaft.",
  },
  
  // Design A Specific Components
  stage2_pulley: {
    id: "stage2_pulley",
    name: "Stage 2 Driven Pulley",
    category: "wheel",
    coords: "Center: (175.0, 30.0) mm",
    zSpan: "41.0 to 53.0 mm",
    dims: "56T HTD-3M | Pitch Ø 53.48 mm | OD 52.7 mm | 12 mm Wide",
    spec: "Aluminum/plastic with flanges",
    machining: "Bolted to skate wheel core via M4 through-bolts",
    desc: "Driven by the Stage 2 pinion on the jackshaft. Positioned on Track 2 (Z=41 to 53 mm). Provides a 3.5:1 secondary reduction stage.",
  },
  jackshaft_pinion: {
    id: "jackshaft_pinion",
    name: "Stage 2 Drive Pinion",
    category: "jackshaft",
    coords: "Center: (145.0, 68.0) mm",
    zSpan: "41.0 to 53.0 mm",
    dims: "16T HTD-3M | Pitch Ø 15.28 mm | OD 14.5 mm | 12 mm Wide",
    spec: "Steel/aluminum, M4 set screw lock",
    machining: "Locked to the 8mm jackshaft on flats",
    desc: "Provides the input pinion for the Stage 2 belt drive. Aligned on Track 2 (Z=41 to 53 mm) opposite the Stage 2 wheel pulley.",
  },
  jackshaft_pulley: {
    id: "jackshaft_pulley",
    name: "Stage 1 Driven Pulley",
    category: "jackshaft",
    coords: "Center: (145.0, 68.0) mm",
    zSpan: "55.0 to 67.0 mm",
    dims: "54T HTD-3M | Pitch Ø 51.57 mm | OD 50.8 mm | 12 mm Wide",
    spec: "Aluminum with flanges, M4 set screw lock",
    machining: "Locked to the 8mm jackshaft",
    desc: "Driven by the Stage 1 belt from the motor. Aligned on Track 1 (Z=55 to 67 mm). Forms the 3.6:1 primary reduction stage.",
  },
  jackshaft_rod: {
    id: "jackshaft_rod",
    name: "Jackshaft Axis Assembly",
    category: "jackshaft",
    coords: "Center: (145.0, 68.0) mm",
    zSpan: "12.0 to 70.0 mm (See note)",
    dims: "Ø 8 mm × 58 mm Long steel rod",
    spec: "Precision ground steel shafting",
    machining: "Supported by two 608RS bearings pressed into plates",
    desc: "Transfer shaft that couples the Stage 1 Driven Pulley to the Stage 2 Pinion. Note: A 58 mm shaft spans exactly between the plates; 70 mm is required for full bearing engagement.",
  },
  motor_body: {
    id: "motor_body",
    name: "550 Electric Motor",
    category: "motor",
    coords: "Center: (95.0, 70.0) mm (Nominal)",
    zSpan: "82.0 to 139.0 mm",
    dims: "Ø 36 mm × 57 mm Can Length",
    spec: "Standard brushed 550 class DC motor",
    machining: "Mounts to inner plate via M3 bolts in horizontal slots",
    desc: "The prime mover. Reaches Y=88 mm, leaving 12 mm ceiling clearance. Standard boss clears through a 13 mm pilot hole. Body extends inward beyond Z=82 mm.",
  },
  motor_pinion: {
    id: "motor_pinion",
    name: "Stage 1 Motor Pinion",
    category: "motor",
    coords: "Center: (95.0, 70.0) mm (Nominal)",
    zSpan: "55.0 to 67.0 mm",
    dims: "15T HTD-3M | Pitch Ø 14.32 mm | OD 13.5 mm | 12 mm Wide",
    spec: "Steel, 3.175 mm (1/8\") bore, set screw",
    machining: "Locked to the motor output shaft",
    desc: "Primary drive pinion. Aligned on Track 1 (Z=55 to 67 mm) to drive the Stage 1 belt loop.",
  },
  belt_stage1: {
    id: "belt_stage1",
    name: "Stage 1 Drive Belt",
    category: "belt",
    coords: "Span: (95,70) to (145,68)",
    zSpan: "55.0 to 64.0 mm",
    dims: "HTD 210-3M-9 | 210 mm pitch length, 70 teeth, 9 mm wide",
    spec: "Fiberglass reinforced rubber, 3mm pitch",
    desc: "Transmits power from the motor pinion (15T) to the jackshaft pulley (54T). Center-to-center distance is 50.04 mm.",
  },
  belt_stage2: {
    id: "belt_stage2",
    name: "Stage 2 Drive Belt",
    category: "belt",
    coords: "Span: (145,68) to (175,30)",
    zSpan: "41.0 to 50.0 mm",
    dims: "HTD 213-3M-9 | 213 mm pitch length, 71 teeth, 9 mm wide",
    spec: "Fiberglass reinforced rubber, 3mm pitch",
    desc: "Transmits power from the jackshaft pinion (16T) to the rear wheel pulley (56T). Center-to-center distance is 48.41 mm.",
  },
  spacing_washer: {
    id: "spacing_washer",
    name: "Axle Spacing Washer",
    category: "chassis",
    coords: "Axles: Front (40,30) & Rear (175,30)",
    zSpan: "12.0 to 14.0 mm",
    dims: "Ø 12 mm Outer / Ø 8.2 mm Inner × 2 mm Thick",
    spec: "Nylon or brass shim",
    desc: "Shims the inline skate wheel away from the outer Baltic Birch plate, ensuring the wheel face does not rub on the timber chassis.",
  },
  coupling_spacer: {
    id: "coupling_spacer",
    name: "Wheel-to-Pulley Coupling Spacer",
    category: "wheel",
    coords: "Rear Axle: (175,30)",
    zSpan: "38.0 to 41.0 mm",
    dims: "Ø 22 mm Outer / Ø 8.2 mm Inner × 3 mm Thick",
    spec: "3D Printed or machined spacer",
    desc: "Provides running clearance between the skate wheel body and the Stage 2 driven pulley. Allows room for bolts and prevents rubbing.",
  },
  thrust_shim: {
    id: "thrust_shim",
    name: "Jackshaft Shaft Collar / Shim",
    category: "jackshaft",
    coords: "Jackshaft: (145,68)",
    zSpan: "67.0 to 70.0 mm",
    dims: "Ø 14 mm Outer / Ø 8 mm Inner × 3 mm Thick",
    spec: "Clamping Shaft Collar & shims",
    desc: "Prevents axial play on the jackshaft. Locks the jackshaft assembly against the inner Baltic Birch side plate bearing inner race.",
  },

  // Design B Specific Components
  worm_gearbox: {
    id: "worm_gearbox",
    name: "5840-341ZY Worm Gearbox Housing",
    category: "gearbox",
    coords: "Center: (175.0, 30.0) mm",
    zSpan: "38.0 to 65.0 mm",
    dims: "58 mm L × 40 mm H × 27 mm W",
    spec: "Right-angle worm gearbox, 27mm thick housing",
    machining: "Bolted to the inner side plate via four M4 machine screws",
    desc: "Houses the high-reduction worm gear set. In this 5840-341ZY model, it is exactly 27 mm thick, leaving ample clearance inside the plates. Sits securely in the space between the side plates and is self-locking.",
  },
  worm_motor: {
    id: "worm_motor",
    name: "341ZY DC Motor Can",
    category: "motor",
    coords: "Span: X=118 to 175 mm | Center Y=30 mm",
    zSpan: "38.0 to 69.0 mm",
    dims: "Ø 31 mm × 57 mm Can Length",
    spec: "12V/24V high-torque permanent magnet brushed DC motor",
    desc: "Runs horizontally along the X-axis, driving the worm gear shaft. It is located inboard of the wheels (Z=38..69 mm) and fits fully inside the Z=85 mm foot envelope.",
  },
  direct_axle: {
    id: "direct_axle",
    name: "Direct Drive Axle (51.5mm Worm Shaft)",
    category: "jackshaft",
    coords: "Center: (175.0, 30.0) mm",
    zSpan: "5.0 to 65.0 mm",
    dims: "Ø 8 mm × 51.5 mm Long steel output shaft",
    spec: "Dual-flat precision output shaft",
    machining: "Supported by bearings in outer plate and gearbox internals",
    desc: "Integrally extends 51.5 mm from the worm gearbox through the wheel hub to the outer plate bearing, forming a highly rigid, direct-drive axle assembly.",
  },
  hub_motor: {
    id: "hub_motor",
    name: "Razor 100W Sensored Brushless Hub Motor",
    category: "motor",
    coords: "Center: (107.5, 38.0) mm | Ground Clearance: 12.0 mm",
    zSpan: "22.5 to 60.5 mm (38mm Wheel) | 12.5 to 70.5 mm (Mounting Ears)",
    dims: "Ø 100 mm Wheel / Ø 80 mm Motor can × 38 mm Wide (+10mm ears each side)",
    spec: "12V 100W Sensored BLDC, integrated solid polyurethane tire, 12mm ground clearance.",
    machining: "Axle clamped onto 10mm mounting ears on each side by custom brackets",
    desc: "A direct-drive brushless hub motor with an integrated 100mm × 38mm wheel, 10mm mounting ears on each side, and 12mm ground clearance below the foot skirt.",
  },
  u_bracket: {
    id: "u_bracket",
    name: "Suspended Axle Mount Brackets",
    category: "chassis",
    coords: "Center: X=107.5 mm | Y: 38 to 85 mm",
    zSpan: "12.5 to 70.5 mm (58mm Span)",
    dims: "58 mm W × 47 mm H × 2 mm Thick",
    spec: "Plywood / Aluminum / 3D-Printed custom foot clamps",
    machining: "Bolted to the foot chassis / top plate to clamp hub motor axle ears",
    desc: "Mounting brackets that clamp the 10mm axle ears on both sides of the 100mm hub motor at centerline X=107.5, Y=38, giving 12mm ground clearance.",
  }
};

// SVG Inversion helper: Converts engineering Y (bottom-left) to SVG Y (top-left)
function toSvgY(userY) {
  return 100 - userY;
}

// Math Helper: Computes outer tangent points between two circles.
function getOuterTangents(x1, y1, r1, x2, y2, r2) {
  const dx = x2 - x1;
  const dy = y2 - y1;
  const d = Math.sqrt(dx * dx + dy * dy);
  
  if (d <= Math.abs(r1 - r2)) return null;
  
  const gamma = Math.atan2(dy, dx);
  const alpha = Math.asin((r2 - r1) / d);
  
  const theta1 = gamma + Math.PI / 2 + alpha;
  const theta2 = gamma + Math.PI / 2 + alpha;
  
  const phi1 = gamma - Math.PI / 2 - alpha;
  const phi2 = gamma - Math.PI / 2 - alpha;
  
  return {
    t1a: { x: x1 + r1 * Math.cos(theta1), y: y1 + r1 * Math.sin(theta1) },
    t2a: { x: x2 + r2 * Math.cos(theta2), y: y2 + r2 * Math.sin(theta2) },
    t2b: { x: x2 + r2 * Math.cos(phi2), y: y2 + r2 * Math.sin(phi2) },
    t1b: { x: x1 + r1 * Math.cos(phi1), y: y1 + r1 * Math.sin(phi1) },
    alpha: alpha,
    gamma: gamma
  };
}

// Draw background grid
function drawGridAndAxes(svg, width, height, xMax, yMax) {
  const g = document.createElementNS("http://www.w3.org/2000/svg", "g");
  g.setAttribute("class", "grid-group");
  
  if (!state.showDimensions) return g;

  for (let x = 0; x <= xMax; x += 10) {
    const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
    line.setAttribute("x1", x);
    line.setAttribute("y1", toSvgY(0));
    line.setAttribute("x2", x);
    line.setAttribute("y2", toSvgY(yMax));
    line.setAttribute("class", x % 50 === 0 ? "grid-line-major" : "grid-line");
    g.appendChild(line);
    
    if (x % 50 === 0 || x === xMax) {
      const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
      text.setAttribute("x", x);
      text.setAttribute("y", toSvgY(0) + 7);
      text.setAttribute("class", "axis-text");
      text.setAttribute("text-anchor", "middle");
      text.textContent = x;
      g.appendChild(text);
    }
  }

  for (let y = 0; y <= yMax; y += 10) {
    const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
    line.setAttribute("x1", 0);
    line.setAttribute("y1", toSvgY(y));
    line.setAttribute("x2", xMax);
    line.setAttribute("y2", toSvgY(y));
    line.setAttribute("class", y % 50 === 0 ? "grid-line-major" : "grid-line");
    g.appendChild(line);

    if (y % 50 === 0 || y === yMax) {
      const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
      text.setAttribute("x", -5);
      text.setAttribute("y", toSvgY(y) + 1.5);
      text.setAttribute("class", "axis-text");
      text.setAttribute("text-anchor", "end");
      text.textContent = y;
      g.appendChild(text);
    }
  }

  const border = document.createElementNS("http://www.w3.org/2000/svg", "rect");
  border.setAttribute("x", 0);
  border.setAttribute("y", 0);
  border.setAttribute("width", xMax);
  border.setAttribute("height", yMax);
  border.setAttribute("fill", "none");
  border.setAttribute("stroke", "rgba(255,255,255,0.15)");
  border.setAttribute("stroke-width", "0.75");
  g.appendChild(border);

  return g;
}

// Setup SVG markers and filters
function setupMarkers(svg) {
  const defs = document.createElementNS("http://www.w3.org/2000/svg", "defs");
  
  const mEnd = document.createElementNS("http://www.w3.org/2000/svg", "marker");
  mEnd.setAttribute("id", "arrow-end");
  mEnd.setAttribute("viewBox", "0 0 10 10");
  mEnd.setAttribute("refX", "8");
  mEnd.setAttribute("refY", "5");
  mEnd.setAttribute("markerWidth", "5");
  mEnd.setAttribute("markerHeight", "5");
  mEnd.setAttribute("orient", "auto-start-reverse");
  const pathEnd = document.createElementNS("http://www.w3.org/2000/svg", "path");
  pathEnd.setAttribute("d", "M 0 1.5 L 8 5 L 0 8.5 z");
  mEnd.appendChild(pathEnd);
  defs.appendChild(mEnd);

  const mStart = document.createElementNS("http://www.w3.org/2000/svg", "marker");
  mStart.setAttribute("id", "arrow-start");
  mStart.setAttribute("viewBox", "0 0 10 10");
  mStart.setAttribute("refX", "2");
  mStart.setAttribute("refY", "5");
  mStart.setAttribute("markerWidth", "5");
  mStart.setAttribute("markerHeight", "5");
  mStart.setAttribute("orient", "auto-start-reverse");
  const pathStart = document.createElementNS("http://www.w3.org/2000/svg", "path");
  pathStart.setAttribute("d", "M 10 1.5 L 2 5 L 10 8.5 z");
  mStart.appendChild(pathStart);
  defs.appendChild(mStart);

  svg.appendChild(defs);
}

// Dimension drawing helper
function createDimLine(x1, y1, x2, y2, offset, text, align = "horizontal") {
  const g = document.createElementNS("http://www.w3.org/2000/svg", "g");
  g.setAttribute("class", "dimension-group");
  if (!state.showDimensions) return g;

  let ext1_x1, ext1_y1, ext1_x2, ext1_y2;
  let ext2_x1, ext2_y1, ext2_x2, ext2_y2;
  let dim_x1, dim_y1, dim_x2, dim_y2;
  let text_x, text_y, text_rot = 0;

  if (align === "horizontal") {
    ext1_x1 = x1; ext1_y1 = y1;
    ext1_x2 = x1; ext1_y2 = y1 - offset - 2;
    ext2_x1 = x2; ext2_y1 = y2;
    ext2_x2 = x2; ext2_y2 = y2 - offset - 2;
    dim_x1 = x1; dim_y1 = y1 - offset;
    dim_x2 = x2; dim_y2 = y2 - offset;
    text_x = (x1 + x2) / 2;
    text_y = dim_y1 - 2;
  } else if (align === "vertical") {
    ext1_x1 = x1; ext1_y1 = y1;
    ext1_x2 = x1 - offset - 2; ext1_y2 = y1;
    ext2_x1 = x2; ext2_y1 = y2;
    ext2_x2 = x2 - offset - 2; ext2_y2 = y2;
    dim_x1 = x1 - offset; dim_y1 = y1;
    dim_x2 = x2 - offset; dim_y2 = y2;
    text_x = dim_x1 - 2;
    text_y = (y1 + y2) / 2 + 1.5;
    text_rot = -90;
  } else if (align === "z-width") {
    ext1_x1 = x1; ext1_y1 = y1;
    ext1_x2 = x1 - offset - 2; ext1_y2 = y1;
    ext2_x1 = x2; ext2_y1 = y2;
    ext2_x2 = x2 - offset - 2; ext2_y2 = y2;
    dim_x1 = x1 - offset; dim_y1 = y1;
    dim_x2 = x2 - offset; dim_y2 = y2;
    text_x = dim_x1 - 2.5;
    text_y = (y1 + y2) / 2 + 1.5;
    text_rot = 0;
  }

  const e1 = document.createElementNS("http://www.w3.org/2000/svg", "line");
  e1.setAttribute("x1", ext1_x1); e1.setAttribute("y1", ext1_y1);
  e1.setAttribute("x2", ext1_x2); e1.setAttribute("y2", ext1_y2);
  e1.setAttribute("class", "dimension-extension");
  g.appendChild(e1);

  const e2 = document.createElementNS("http://www.w3.org/2000/svg", "line");
  e2.setAttribute("x1", ext2_x1); e2.setAttribute("y1", ext2_y1);
  e2.setAttribute("x2", ext2_x2); e2.setAttribute("y2", ext2_y2);
  e2.setAttribute("class", "dimension-extension");
  g.appendChild(e2);

  const dl = document.createElementNS("http://www.w3.org/2000/svg", "line");
  dl.setAttribute("x1", dim_x1); dl.setAttribute("y1", dim_y1);
  dl.setAttribute("x2", dim_x2); dl.setAttribute("y2", dim_y2);
  dl.setAttribute("class", "dimension-line");
  g.appendChild(dl);

  const dtBg = document.createElementNS("http://www.w3.org/2000/svg", "text");
  dtBg.setAttribute("x", text_x);
  dtBg.setAttribute("y", text_y);
  dtBg.setAttribute("class", "dimension-text bg-shield");
  if (text_rot !== 0) {
    dtBg.setAttribute("transform", `rotate(${text_rot}, ${text_x}, ${text_y})`);
  }
  dtBg.textContent = text;
  g.appendChild(dtBg);

  const dt = document.createElementNS("http://www.w3.org/2000/svg", "text");
  dt.setAttribute("x", text_x);
  dt.setAttribute("y", text_y);
  dt.setAttribute("class", "dimension-text");
  if (text_rot !== 0) {
    dt.setAttribute("transform", `rotate(${text_rot}, ${text_x}, ${text_y})`);
  }
  dt.textContent = text;
  g.appendChild(dt);

  return g;
}

// Bind hover interactions
function bindHoverEvents(element, componentId) {
  element.addEventListener("mouseenter", () => {
    state.hoveredComponent = componentId;
    updateInspector(componentId);
    highlightSync();
  });
  element.addEventListener("mouseleave", () => {
    state.hoveredComponent = null;
    clearInspector();
    highlightSync();
  });
}

function highlightSync() {
  const elements = document.querySelectorAll(".interactive-element");
  elements.forEach(el => {
    const compId = el.getAttribute("data-component-id");
    if (state.hoveredComponent && compId === state.hoveredComponent) {
      el.classList.add("highlighted");
    } else {
      el.classList.remove("highlighted");
    }
  });

  const belts = document.querySelectorAll(".belt-line");
  belts.forEach(belt => {
    const beltId = belt.getAttribute("data-component-id");
    if (state.hoveredComponent && beltId === state.hoveredComponent) {
      belt.classList.add("highlighted");
    } else {
      belt.classList.remove("highlighted");
    }
  });
}

function updateInspector(id) {
  const comp = COMPONENTS[id];
  if (!comp) return;

  const placeholder = document.getElementById("inspector-placeholder");
  const content = document.getElementById("inspector-content");
  placeholder.classList.add("hidden");
  content.classList.remove("hidden");

  content.className = "inspector-details " + comp.category;

  document.getElementById("inspect-name").textContent = comp.name;
  document.getElementById("inspect-coords").textContent = comp.coords.replace(" | ", "\n");
  document.getElementById("inspect-z-span").textContent = comp.zSpan;
  document.getElementById("inspect-dims").textContent = comp.dims;
  document.getElementById("inspect-spec").textContent = comp.spec;
  document.getElementById("inspect-desc").textContent = comp.desc;

  const machiningRow = document.getElementById("inspect-machining-row");
  if (comp.machining) {
    machiningRow.classList.remove("hidden");
    document.getElementById("inspect-machining").textContent = comp.machining;
  } else {
    machiningRow.classList.add("hidden");
  }
}

function clearInspector() {
  const placeholder = document.getElementById("inspector-placeholder");
  const content = document.getElementById("inspector-content");
  placeholder.classList.remove("hidden");
  content.classList.add("hidden");
}

// ----------------------------------------------------
// VIEW 1: SIDE PROFILE (XY-PLANE)
// ----------------------------------------------------
function drawSideView() {
  const svg = document.getElementById("svg-side-view");
  svg.innerHTML = "";
  setupMarkers(svg);

  svg.appendChild(drawGridAndAxes(svg, 215, 100, 215, 100));

  const mainG = document.createElementNS("http://www.w3.org/2000/svg", "g");

  // 1. CLEARANCE BOUNDARIES
  if (state.showClearances) {
    // Cavity boundary outline
    const cavity = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    cavity.setAttribute("x", 0);
    cavity.setAttribute("y", toSvgY(100));
    cavity.setAttribute("width", 215);
    cavity.setAttribute("height", 100);
    cavity.setAttribute("class", "chassis-boundary-solid interactive-element");
    cavity.setAttribute("data-component-id", "chassis_box");
    bindHoverEvents(cavity, "chassis_box");
    mainG.appendChild(cavity);

    // Side plates boundary (X=2.5..212.5, Y=10..95)
    const plateOutline = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    plateOutline.setAttribute("x", 2.5);
    plateOutline.setAttribute("y", toSvgY(95));
    plateOutline.setAttribute("width", 210);
    plateOutline.setAttribute("height", 85);
    plateOutline.setAttribute("class", "chassis-boundary interactive-element");
    plateOutline.setAttribute("data-component-id", "outer_plate");
    bindHoverEvents(plateOutline, "outer_plate");
    mainG.appendChild(plateOutline);
  }

  if (state.currentDesign !== "C") {
    // 2. FRONT WHEEL
    const frontW = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    frontW.setAttribute("cx", 40);
    frontW.setAttribute("cy", toSvgY(30));
    frontW.setAttribute("r", 30);
    frontW.setAttribute("class", "interactive-element wheel");
    frontW.setAttribute("data-component-id", "front_wheel");
    bindHoverEvents(frontW, "front_wheel");
    mainG.appendChild(frontW);

    const frontBear = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    frontBear.setAttribute("cx", 40);
    frontBear.setAttribute("cy", toSvgY(30));
    frontBear.setAttribute("r", 4);
    frontBear.setAttribute("fill", "#64748b");
    mainG.appendChild(frontBear);

    // 3. REAR DRIVE WHEEL
    const rearW = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    rearW.setAttribute("cx", 175);
    rearW.setAttribute("cy", toSvgY(30));
    rearW.setAttribute("r", 30);
    rearW.setAttribute("class", "interactive-element wheel");
    rearW.setAttribute("data-component-id", "rear_wheel");
    bindHoverEvents(rearW, "rear_wheel");
    mainG.appendChild(rearW);

    // Rear Bearing Center
    const rearBear = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    rearBear.setAttribute("cx", 175);
    rearBear.setAttribute("cy", toSvgY(30));
    rearBear.setAttribute("r", 4);
    rearBear.setAttribute("fill", "#64748b");
    mainG.appendChild(rearBear);
  }

  if (state.currentDesign === "A") {
    // ---------------------------------
    // DESIGN A: BELT DRIVE
    // ---------------------------------
    if (state.designAStages === 1) {
      // ---------------------------------
      // DESIGN A: 1-STAGE BELT DRIVE (Direct)
      // ---------------------------------
      
      // Stage 1 Driven Pulley (52.7 mm OD) on rear wheel
      const rPulley = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      rPulley.setAttribute("cx", 175);
      rPulley.setAttribute("cy", toSvgY(30));
      rPulley.setAttribute("r", 52.7 / 2);
      rPulley.setAttribute("class", "pulley-body interactive-element wheel");
      rPulley.setAttribute("data-component-id", "stage2_pulley");
      bindHoverEvents(rPulley, "stage2_pulley");
      mainG.appendChild(rPulley);

      // Motor can (Ø36 for 550/3650, Ø35 for 3536) at (107.5, 30)
      let motorRadius = 18;
      if (state.designAMotor === "3536") {
        motorRadius = 17.5;
      }
      const motorCan = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      motorCan.setAttribute("cx", 107.5);
      motorCan.setAttribute("cy", toSvgY(30));
      motorCan.setAttribute("r", motorRadius);
      motorCan.setAttribute("class", "interactive-element motor");
      motorCan.setAttribute("data-component-id", "motor_body");
      bindHoverEvents(motorCan, "motor_body");
      mainG.appendChild(motorCan);

      // Motor Pinion (Ø13.5)
      const mPinion = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      mPinion.setAttribute("cx", 107.5);
      mPinion.setAttribute("cy", toSvgY(30));
      mPinion.setAttribute("r", 13.5 / 2);
      mPinion.setAttribute("class", "pulley-body interactive-element motor");
      mPinion.setAttribute("data-component-id", "motor_pinion");
      bindHoverEvents(mPinion, "motor_pinion");
      mainG.appendChild(mPinion);

      // Direct Belt (107.5,30 to 175,30)
      const st1Belt = getOuterTangents(107.5, 30, 14.32 / 2, 175, 30, 53.48 / 2);
      if (st1Belt && state.showBelts) {
        const p = st1Belt;
        const pathStr = `M ${p.t1a.x} ${toSvgY(p.t1a.y)} L ${p.t2a.x} ${toSvgY(p.t2a.y)} A ${53.48 / 2} ${53.48 / 2} 0 1 1 ${p.t2b.x} ${toSvgY(p.t2b.y)} L ${p.t1b.x} ${toSvgY(p.t1b.y)} A ${14.32 / 2} ${14.32 / 2} 0 0 0 ${p.t1a.x} ${toSvgY(p.t1a.y)} Z`;
        
        const bTrack = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bTrack.setAttribute("d", pathStr);
        bTrack.setAttribute("class", "belt-track");
        mainG.appendChild(bTrack);

        const bLine = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bLine.setAttribute("d", pathStr);
        bLine.setAttribute("class", "belt-line stage1");
        bLine.setAttribute("data-component-id", "belt_stage1");
        bindHoverEvents(bLine, "belt_stage1");
        mainG.appendChild(bLine);

        const bTeeth = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bTeeth.setAttribute("d", pathStr);
        bTeeth.setAttribute("class", "belt-teeth running");
        bTeeth.setAttribute("data-component-id", "belt_stage1");
        mainG.appendChild(bTeeth);
      }

      if (state.showDimensions) {
        mainG.appendChild(createDimLine(40, 30, 175, 30, 20, "135.0 mm", "horizontal"));
        mainG.appendChild(createDimLine(0, 30, 40, 30, 15, "40.0 mm", "horizontal"));
        mainG.appendChild(createDimLine(40, 0, 40, 30, 15, "30.0 mm", "vertical"));
        mainG.appendChild(createDimLine(175, 30, 215, 30, 20, "40.0 mm", "horizontal"));
        mainG.appendChild(createDimLine(0, 30, 107.5, 30, -25, "107.5 mm", "horizontal"));
        mainG.appendChild(createDimLine(107.5, 0, 107.5, 30, -10, "30.0 mm", "vertical"));
      }

      if (state.showLabels) {
        const addLabel = (x, y, text, title) => {
          const tg = document.createElementNS("http://www.w3.org/2000/svg", "g");
          const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
          t.setAttribute("x", x); t.setAttribute("y", toSvgY(y));
          t.setAttribute("class", "label-title"); t.setAttribute("text-anchor", "middle");
          t.textContent = title; tg.appendChild(t);
          const d = document.createElementNS("http://www.w3.org/2000/svg", "text");
          d.setAttribute("x", x); d.setAttribute("y", toSvgY(y) + 4.5);
          d.setAttribute("class", "label-text"); d.setAttribute("text-anchor", "middle");
          d.textContent = text; tg.appendChild(d);
          mainG.appendChild(tg);
        };
        addLabel(40, 5, "Front Idler", "Ø60 Skate Wheel");
        addLabel(175, 5, "Rear Drive", "Ø60 + 56T Pulley");
        
        let mName = "550 Motor";
        if (state.designAMotor === "3536") mName = "3536 Motor";
        else if (state.designAMotor === "3650") mName = "3650 Motor";
        addLabel(107.5, 75, `${mName} (15T)`, "X=107.5, Y=30");
      }

    } else {
      // ---------------------------------
      // DESIGN A: 2-STAGE BELT DRIVE (Jackshaft)
      // ---------------------------------
      
      // Stage 2 Driven Pulley (52.7 mm OD)
      const rPulley = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      rPulley.setAttribute("cx", 175);
      rPulley.setAttribute("cy", toSvgY(30));
      rPulley.setAttribute("r", 52.7 / 2);
      rPulley.setAttribute("class", "pulley-body interactive-element wheel");
      rPulley.setAttribute("data-component-id", "stage2_pulley");
      bindHoverEvents(rPulley, "stage2_pulley");
      mainG.appendChild(rPulley);

      // Jackshaft Pulley (Ø50.8) and Pinion (Ø14.5) at (145, 68)
      const jPulley = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      jPulley.setAttribute("cx", 145);
      jPulley.setAttribute("cy", toSvgY(68));
      jPulley.setAttribute("r", 50.8 / 2);
      jPulley.setAttribute("class", "pulley-body interactive-element jackshaft");
      jPulley.setAttribute("data-component-id", "jackshaft_pulley");
      bindHoverEvents(jPulley, "jackshaft_pulley");
      mainG.appendChild(jPulley);

      const jPinion = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      jPinion.setAttribute("cx", 145);
      jPinion.setAttribute("cy", toSvgY(68));
      jPinion.setAttribute("r", 14.5 / 2);
      jPinion.setAttribute("class", "pulley-body interactive-element jackshaft");
      jPinion.setAttribute("data-component-id", "jackshaft_pinion");
      bindHoverEvents(jPinion, "jackshaft_pinion");
      mainG.appendChild(jPinion);

      const jCenter = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      jCenter.setAttribute("cx", 145);
      jCenter.setAttribute("cy", toSvgY(68));
      jCenter.setAttribute("r", 4);
      jCenter.setAttribute("class", "interactive-element jackshaft");
      jCenter.setAttribute("data-component-id", "jackshaft_rod");
      bindHoverEvents(jCenter, "jackshaft_rod");
      mainG.appendChild(jCenter);

      // Motor can (Ø36 for 550/3650, Ø35 for 3536) at (95,70)
      let motorRadius = 18;
      if (state.designAMotor === "3536") {
        motorRadius = 17.5;
      }
      const motorCan = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      motorCan.setAttribute("cx", 95);
      motorCan.setAttribute("cy", toSvgY(70));
      motorCan.setAttribute("r", motorRadius);
      motorCan.setAttribute("class", "interactive-element motor");
      motorCan.setAttribute("data-component-id", "motor_body");
      bindHoverEvents(motorCan, "motor_body");
      mainG.appendChild(motorCan);

      // Motor Pinion (Ø13.5)
      const mPinion = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      mPinion.setAttribute("cx", 95);
      mPinion.setAttribute("cy", toSvgY(70));
      mPinion.setAttribute("r", 13.5 / 2);
      mPinion.setAttribute("class", "pulley-body interactive-element motor");
      mPinion.setAttribute("data-component-id", "motor_pinion");
      bindHoverEvents(mPinion, "motor_pinion");
      mainG.appendChild(mPinion);

      // Stage 1 Belt (95,70 to 145,68)
      const st1Belt = getOuterTangents(95, 70, 14.32 / 2, 145, 68, 51.57 / 2);
      if (st1Belt && state.showBelts) {
        const p = st1Belt;
        const pathStr = `M ${p.t1a.x} ${toSvgY(p.t1a.y)} L ${p.t2a.x} ${toSvgY(p.t2a.y)} A ${51.57 / 2} ${51.57 / 2} 0 1 1 ${p.t2b.x} ${toSvgY(p.t2b.y)} L ${p.t1b.x} ${toSvgY(p.t1b.y)} A ${14.32 / 2} ${14.32 / 2} 0 0 0 ${p.t1a.x} ${toSvgY(p.t1a.y)} Z`;
        
        const bTrack = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bTrack.setAttribute("d", pathStr);
        bTrack.setAttribute("class", "belt-track");
        mainG.appendChild(bTrack);

        const bLine = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bLine.setAttribute("d", pathStr);
        bLine.setAttribute("class", "belt-line stage1");
        bLine.setAttribute("data-component-id", "belt_stage1");
        bindHoverEvents(bLine, "belt_stage1");
        mainG.appendChild(bLine);

        const bTeeth = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bTeeth.setAttribute("d", pathStr);
        bTeeth.setAttribute("class", "belt-teeth running fast");
        bTeeth.setAttribute("data-component-id", "belt_stage1");
        mainG.appendChild(bTeeth);
      }

      // Stage 2 Belt (145,68 to 175,30)
      const st2Belt = getOuterTangents(145, 68, 15.28 / 2, 175, 30, 53.48 / 2);
      if (st2Belt && state.showBelts) {
        const p = st2Belt;
        const pathStr = `M ${p.t1a.x} ${toSvgY(p.t1a.y)} L ${p.t2a.x} ${toSvgY(p.t2a.y)} A ${53.48 / 2} ${53.48 / 2} 0 1 1 ${p.t2b.x} ${toSvgY(p.t2b.y)} L ${p.t1b.x} ${toSvgY(p.t1b.y)} A ${15.28 / 2} ${15.28 / 2} 0 0 0 ${p.t1a.x} ${toSvgY(p.t1a.y)} Z`;

        const bTrack = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bTrack.setAttribute("d", pathStr);
        bTrack.setAttribute("class", "belt-track");
        mainG.appendChild(bTrack);

        const bLine = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bLine.setAttribute("d", pathStr);
        bLine.setAttribute("class", "belt-line stage2");
        bLine.setAttribute("data-component-id", "belt_stage2");
        bindHoverEvents(bLine, "belt_stage2");
        mainG.appendChild(bLine);

        const bTeeth = document.createElementNS("http://www.w3.org/2000/svg", "path");
        bTeeth.setAttribute("d", pathStr);
        bTeeth.setAttribute("class", "belt-teeth running");
        bTeeth.setAttribute("data-component-id", "belt_stage2");
        mainG.appendChild(bTeeth);
      }

      if (state.showDimensions) {
        mainG.appendChild(createDimLine(40, 30, 175, 30, 20, "135.0 mm", "horizontal"));
        mainG.appendChild(createDimLine(0, 30, 40, 30, 15, "40.0 mm", "horizontal"));
        mainG.appendChild(createDimLine(40, 0, 40, 30, 15, "30.0 mm", "vertical"));
        mainG.appendChild(createDimLine(175, 30, 215, 30, 20, "40.0 mm", "horizontal"));
        mainG.appendChild(createDimLine(0, 68, 145, 68, -15, "145.0 mm", "horizontal"));
        mainG.appendChild(createDimLine(145, 0, 145, 68, 10, "68.0 mm", "vertical"));
        mainG.appendChild(createDimLine(0, 70, 95, 70, -25, "95.0 mm", "horizontal"));
        mainG.appendChild(createDimLine(95, 0, 95, 70, -10, "70.0 mm", "vertical"));
      }

      if (state.showLabels) {
        const addLabel = (x, y, text, title) => {
          const tg = document.createElementNS("http://www.w3.org/2000/svg", "g");
          const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
          t.setAttribute("x", x); t.setAttribute("y", toSvgY(y));
          t.setAttribute("class", "label-title"); t.setAttribute("text-anchor", "middle");
          t.textContent = title; tg.appendChild(t);
          const d = document.createElementNS("http://www.w3.org/2000/svg", "text");
          d.setAttribute("x", x); d.setAttribute("y", toSvgY(y) + 4.5);
          d.setAttribute("class", "label-text"); d.setAttribute("text-anchor", "middle");
          d.textContent = text; tg.appendChild(d);
          mainG.appendChild(tg);
        };
        addLabel(40, 5, "Front Idler", "Ø60 Skate Wheel");
        addLabel(175, 5, "Rear Drive", "Ø60 + 56T Pulley");
        addLabel(145, 80, "Jackshaft (54T/16T)", "X=145, Y=68");
        
        let mName = "550 Motor";
        if (state.designAMotor === "3536") mName = "3536 Motor";
        else if (state.designAMotor === "3650") mName = "3650 Motor";
        addLabel(95, 93, `${mName} (15T)`, "X=95, Y=70");
      }
    }
  } else if (state.currentDesign === "B") {
    // ---------------------------------
    // DESIGN B: DIRECT WORM DRIVE
    // ---------------------------------

    // Worm Gearbox Profile Casing (58 mm long x 40 mm tall)
    const gBox = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    gBox.setAttribute("x", 137.1);
    gBox.setAttribute("y", toSvgY(50));
    gBox.setAttribute("width", 58);
    gBox.setAttribute("height", 40);
    gBox.setAttribute("rx", 3);
    gBox.setAttribute("class", "interactive-element gearbox");
    gBox.setAttribute("data-component-id", "worm_gearbox");
    bindHoverEvents(gBox, "worm_gearbox");
    mainG.appendChild(gBox);

    // Motor cylinder body running forward (diameter 31 mm, centered on axle at Y=30.0)
    const wMotor = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    wMotor.setAttribute("x", 80.1);
    wMotor.setAttribute("y", toSvgY(45.5));
    wMotor.setAttribute("width", 57);
    wMotor.setAttribute("height", 31);
    wMotor.setAttribute("rx", 2);
    wMotor.setAttribute("class", "interactive-element motor");
    wMotor.setAttribute("data-component-id", "worm_motor");
    bindHoverEvents(wMotor, "worm_motor");
    mainG.appendChild(wMotor);

    // Motor end cap / terminal details
    const wCap = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    wCap.setAttribute("x", 77.1);
    wCap.setAttribute("y", toSvgY(30.0 + 11));
    wCap.setAttribute("width", 3);
    wCap.setAttribute("height", 22);
    wCap.setAttribute("fill", "#050a14");
    wCap.setAttribute("stroke", "var(--color-motor)");
    wCap.setAttribute("stroke-width", "0.5");
    mainG.appendChild(wCap);

    if (state.showDimensions) {
      mainG.appendChild(createDimLine(40, 30, 175, 30, 20, "135.0 mm Axle Span", "horizontal"));
      mainG.appendChild(createDimLine(80.1, 30.0, 195.1, 30.0, -10, "115.0 mm Gearmotor Length", "horizontal"));
      mainG.appendChild(createDimLine(80.1, 30.0, 175, 30.0, -22, "94.9 mm Axle-to-Front Offset", "horizontal"));
      mainG.appendChild(createDimLine(70, 30.0, 80.1, 30.0, -34, "10.1 mm Wheel Gap", "horizontal"));
    }

    if (state.showLabels) {
      const addLabel = (x, y, text, title) => {
        const tg = document.createElementNS("http://www.w3.org/2000/svg", "g");
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", x); t.setAttribute("y", toSvgY(y));
        t.setAttribute("class", "label-title"); t.setAttribute("text-anchor", "middle");
        t.textContent = title; tg.appendChild(t);
        const d = document.createElementNS("http://www.w3.org/2000/svg", "text");
        d.setAttribute("x", x); d.setAttribute("y", toSvgY(y) + 4.5);
        d.setAttribute("class", "label-text"); d.setAttribute("text-anchor", "middle");
        d.textContent = text; tg.appendChild(d);
        mainG.appendChild(tg);
      };
      addLabel(40, 5, "Front Idler", "Ø60 Skate Wheel");
      addLabel(175, 5, "Rear Drive Axle", "Ø60 Direct Drive");
      addLabel(160, 62, "5840 Gearbox", "Output Shaft Offset");
      addLabel(105, 62, "31ZY Motor", "Parallel to X");
    }
  } else if (state.currentDesign === "C") {
    // ---------------------------------
    // DESIGN C: RAZOR HUB MOTOR & U-BRACKET (Single Centered Wheel)
    // ---------------------------------

    // Suspended Axle Mount Bracket (from Y=38 to Y=85, width 35mm centered at X=107.5)
    const uBrack = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    uBrack.setAttribute("x", 90);
    uBrack.setAttribute("y", toSvgY(85));
    uBrack.setAttribute("width", 35);
    uBrack.setAttribute("height", 47);
    uBrack.setAttribute("rx", 3);
    uBrack.setAttribute("class", "interactive-element chassis-plate");
    uBrack.setAttribute("fill", "rgba(180, 139, 101, 0.1)");
    uBrack.setAttribute("stroke", "var(--color-chassis)");
    uBrack.setAttribute("stroke-width", "1");
    uBrack.setAttribute("data-component-id", "u_bracket");
    bindHoverEvents(uBrack, "u_bracket");
    mainG.appendChild(uBrack);

    // Razor Hub Motor Assembly (Solid Outer Tyre Ø100 mm, centered at X=107.5, Y=38)
    const rWheel = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    rWheel.setAttribute("cx", 107.5);
    rWheel.setAttribute("cy", toSvgY(38));
    rWheel.setAttribute("r", 50); // Ø100 mm (12mm ground clearance below Y=0)
    rWheel.setAttribute("class", "pulley-body interactive-element wheel");
    rWheel.setAttribute("data-component-id", "hub_motor");
    bindHoverEvents(rWheel, "hub_motor");
    mainG.appendChild(rWheel);

    // Motor Hub Inner Casing (Ø80 mm)
    const rMotor = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    rMotor.setAttribute("cx", 107.5);
    rMotor.setAttribute("cy", toSvgY(38));
    rMotor.setAttribute("r", 40); // Ø80
    rMotor.setAttribute("class", "interactive-element motor");
    rMotor.setAttribute("data-component-id", "hub_motor");
    bindHoverEvents(rMotor, "hub_motor");
    mainG.appendChild(rMotor);

    // Axle Nut Center (Ø8 mm at X=107.5, Y=38)
    const axleNut = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    axleNut.setAttribute("cx", 107.5);
    axleNut.setAttribute("cy", toSvgY(38));
    axleNut.setAttribute("r", 4);
    axleNut.setAttribute("fill", "#666");
    axleNut.setAttribute("stroke", "#999");
    mainG.appendChild(axleNut);

    if (state.showDimensions) {
      mainG.appendChild(createDimLine(0, 38, 107.5, 38, -25, "107.5 mm Centerline", "horizontal"));
      mainG.appendChild(createDimLine(107.5, 38, 215, 38, -25, "107.5 mm Centerline", "horizontal"));
      mainG.appendChild(createDimLine(107.5, 0, 107.5, -12, 25, "12.0 mm Ground Clearance", "vertical"));
      mainG.appendChild(createDimLine(90, 38, 90, 85, -10, "47.0 mm Bracket Height", "vertical"));
    }

    if (state.showLabels) {
      const addLabel = (x, y, text, title) => {
        const tg = document.createElementNS("http://www.w3.org/2000/svg", "g");
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", x); t.setAttribute("y", toSvgY(y));
        t.setAttribute("class", "label-title"); t.setAttribute("text-anchor", "middle");
        t.textContent = title; tg.appendChild(t);
        const d = document.createElementNS("http://www.w3.org/2000/svg", "text");
        d.setAttribute("x", x); d.setAttribute("y", toSvgY(y) + 4.5);
        d.setAttribute("class", "label-text"); d.setAttribute("text-anchor", "middle");
        d.textContent = text; tg.appendChild(d);
        mainG.appendChild(tg);
      };
      addLabel(107.5, -20, "Razor Hub Motor", "Ø100 × 38mm Wheel (100W)");
      addLabel(107.5, 100, "Centerline Mount", "10mm Ears (12mm Clearance)");
    }
  }

  svg.appendChild(mainG);
}

// ----------------------------------------------------
// VIEW 2: TOP PLAN VIEW (XZ-PLANE)
// ----------------------------------------------------
function drawTopView() {
  const svg = document.getElementById("svg-top-view");
  svg.innerHTML = "";
  setupMarkers(svg);

  const gGrid = document.createElementNS("http://www.w3.org/2000/svg", "g");
  if (state.showDimensions) {
    for (let x = 0; x <= 215; x += 10) {
      const l = document.createElementNS("http://www.w3.org/2000/svg", "line");
      l.setAttribute("x1", x); l.setAttribute("y1", 0);
      l.setAttribute("x2", x); l.setAttribute("y2", 85);
      l.setAttribute("class", x % 50 === 0 ? "grid-line-major" : "grid-line");
      gGrid.appendChild(l);
      if (x % 50 === 0 || x === 215) {
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", x); t.setAttribute("y", 92);
        t.setAttribute("class", "axis-text");
        t.setAttribute("text-anchor", "middle");
        t.textContent = x;
        gGrid.appendChild(t);
      }
    }
    for (let z = 0; z <= 85; z += 10) {
      const l = document.createElementNS("http://www.w3.org/2000/svg", "line");
      l.setAttribute("x1", 0); l.setAttribute("y1", z);
      l.setAttribute("x2", 215); l.setAttribute("y2", z);
      l.setAttribute("class", z % 50 === 0 ? "grid-line-major" : "grid-line");
      gGrid.appendChild(l);
      const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
      t.setAttribute("x", -5); t.setAttribute("y", z + 1.5);
      t.setAttribute("class", "axis-text");
      t.setAttribute("text-anchor", "end");
      t.textContent = z;
      gGrid.appendChild(t);
    }
  }
  
  const envelope = document.createElementNS("http://www.w3.org/2000/svg", "rect");
  envelope.setAttribute("x", 0);
  envelope.setAttribute("y", 0);
  envelope.setAttribute("width", 215);
  envelope.setAttribute("height", 85);
  envelope.setAttribute("fill", "none");
  envelope.setAttribute("stroke", "rgba(255,255,255,0.15)");
  envelope.setAttribute("stroke-width", "0.75");
  gGrid.appendChild(envelope);
  svg.appendChild(gGrid);

  const mainG = document.createElementNS("http://www.w3.org/2000/svg", "g");

  function drawBlock(x1, z1, x2, z2, className, compId) {
    const r = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    r.setAttribute("x", Math.min(x1, x2));
    r.setAttribute("y", Math.min(z1, z2));
    r.setAttribute("width", Math.abs(x2 - x1));
    r.setAttribute("height", Math.abs(z2 - z1));
    r.setAttribute("class", "interactive-element " + className);
    r.setAttribute("data-component-id", compId);
    bindHoverEvents(r, compId);
    mainG.appendChild(r);
    return r;
  }

  // 1. FRONT WHEEL ASSEMBLY (Identical in both designs)
  // Outer Plate (Z=0..12)
  drawBlock(2.5, 0, 212.5, 12, "chassis-plate", "outer_plate");
  if (state.currentDesign !== "C") {
    // Bearings Outer
    drawBlock(34, 5, 46, 12, "bearing-body", "outer_plate");
    drawBlock(36, 5, 44, 12, "bearing-inner", "outer_plate");

    // Front Axle rod (Z=0..82)
    const fAxle = document.createElementNS("http://www.w3.org/2000/svg", "line");
    fAxle.setAttribute("x1", 40); fAxle.setAttribute("y1", 0);
    fAxle.setAttribute("x2", 40); fAxle.setAttribute("y2", 82);
    fAxle.setAttribute("class", "shaft-rod");
    fAxle.setAttribute("stroke-width", "2");
    mainG.appendChild(fAxle);

    drawBlock(37, 12, 43, 14, "spacing-washer", "spacing_washer");
    drawBlock(10, 14, 70, 38, "wheel", "front_wheel");
  }

  if (state.currentDesign === "A") {
    // ---------------------------------
    // DESIGN A: BELT DRIVE
    // ---------------------------------
    
    // Inner Plate (Z=70..82)
    drawBlock(2.5, 70, 212.5, 82, "chassis-plate", "inner_plate");
    
    // Front Axle inner support bearing
    drawBlock(34, 70, 46, 77, "bearing-body", "inner_plate");
    drawBlock(36, 70, 44, 77, "bearing-inner", "inner_plate");

    // Rear Axle rod (Z=0..82)
    const rAxle = document.createElementNS("http://www.w3.org/2000/svg", "line");
    rAxle.setAttribute("x1", 175); rAxle.setAttribute("y1", 0);
    rAxle.setAttribute("x2", 175); rAxle.setAttribute("y2", 82);
    rAxle.setAttribute("class", "shaft-rod");
    rAxle.setAttribute("stroke-width", "2");
    mainG.appendChild(rAxle);

    // Rear bearings
    drawBlock(169, 5, 181, 12, "bearing-body", "outer_plate");
    drawBlock(171, 5, 179, 12, "bearing-inner", "outer_plate");
    drawBlock(169, 70, 181, 77, "bearing-body", "inner_plate");
    drawBlock(171, 70, 179, 77, "bearing-inner", "inner_plate");

    // Wheel
    drawBlock(172, 12, 178, 14, "spacing-washer", "spacing_washer");
    drawBlock(145, 14, 205, 38, "wheel", "rear_wheel");

    if (state.designAStages === 1) {
      // 1-Stage Direct Belt
      drawBlock(169, 38, 181, 55, "coupling-spacer", "coupling_spacer");
      drawBlock(175 - 52.7/2, 55, 175 + 52.7/2, 67, "wheel", "stage2_pulley");

      // Motor (Dynamic size, orientation, and collision warning) centered at X=107.5
      let motorLen = 57;
      let motorRad = 18;
      if (state.designAMotor === "3536") {
        motorLen = 36;
        motorRad = 17.5;
      } else if (state.designAMotor === "3650") {
        motorLen = 50;
        motorRad = 18;
      }

      if (state.designAMount === "between") {
        // Motor face is at Z=55, extending outboard (towards Z=12)
        drawBlock(107.5 - 6.5, 55, 107.5 + 6.5, 67, "motor", "motor_body");
        drawBlock(107.5 - 13.5/2, 55, 107.5 + 13.5/2, 67, "motor", "motor_pinion");
        
        const motorStart = 55 - motorLen;
        if (motorStart >= 12) {
          drawBlock(107.5 - motorRad, motorStart, 107.5 + motorRad, 55, "motor", "motor_body");
        } else {
          drawBlock(107.5 - motorRad, 12, 107.5 + motorRad, 55, "motor", "motor_body");
          drawBlock(107.5 - motorRad, motorStart, 107.5 + motorRad, 12, "collision-warning", "motor_body");
        }
      } else {
        // Standard Inboard Mount: Motor face is at Z=82, body extends inboard
        drawBlock(107.5 - 6.5, 70, 107.5 + 6.5, 82, "motor", "motor_body");
        drawBlock(107.5 - motorRad, 82, 107.5 + motorRad, 82 + motorLen, "motor", "motor_body");
        drawBlock(107.5 - 13.5/2, 55, 107.5 + 13.5/2, 67, "motor", "motor_pinion");
      }

      if (state.showBelts) {
        const belt1 = document.createElementNS("http://www.w3.org/2000/svg", "rect");
        belt1.setAttribute("x", 100.75); belt1.setAttribute("y", 55 + 1);
        belt1.setAttribute("width", 100.6); belt1.setAttribute("height", 9);
        belt1.setAttribute("fill", "rgba(0, 255, 102, 0.15)"); belt1.setAttribute("stroke", "var(--color-belt1)");
        belt1.setAttribute("class", "interactive-element"); belt1.setAttribute("data-component-id", "belt_stage1");
        bindHoverEvents(belt1, "belt_stage1"); mainG.appendChild(belt1);
      }

      if (state.showClearances) {
        const insideMargin = document.createElementNS("http://www.w3.org/2000/svg", "rect");
        insideMargin.setAttribute("x", 0); insideMargin.setAttribute("y", 82);
        insideMargin.setAttribute("width", 215); insideMargin.setAttribute("height", 3);
        insideMargin.setAttribute("fill", "rgba(255, 0, 85, 0.05)"); insideMargin.setAttribute("stroke", "#ff0055");
        insideMargin.setAttribute("stroke-dasharray", "1, 1"); mainG.appendChild(insideMargin);
      }

      if (state.showDimensions) {
        mainG.appendChild(createDimLine(2.5, 0, 2.5, 12, 10, "12 mm Outer Plate", "z-width"));
        mainG.appendChild(createDimLine(175, 14, 175, 38, 15, "24 mm Wheel", "z-width"));
        mainG.appendChild(createDimLine(175, 55, 175, 67, 20, "12 mm Belt Pulley", "z-width"));
        mainG.appendChild(createDimLine(2.5, 70, 2.5, 82, 10, "12 mm Inner Plate", "z-width"));
        mainG.appendChild(createDimLine(212.5, 0, 212.5, 85, -15, "85 mm Envelope", "z-width"));
      }

    } else {
      // 2-Stage Jackshaft
      drawBlock(169, 38, 181, 41, "coupling-spacer", "coupling_spacer");
      drawBlock(175 - 52.7/2, 41, 175 + 52.7/2, 53, "wheel", "stage2_pulley");

      // Jackshaft assembly
      const jsRod = document.createElementNS("http://www.w3.org/2000/svg", "rect");
      jsRod.setAttribute("x", 144); jsRod.setAttribute("y", 12);
      jsRod.setAttribute("width", 2); jsRod.setAttribute("height", 58);
      jsRod.setAttribute("class", "interactive-element shaft-rod");
      jsRod.setAttribute("data-component-id", "jackshaft_rod");
      bindHoverEvents(jsRod, "jackshaft_rod");
      mainG.appendChild(jsRod);

      if (state.showClearances) {
        const rodErr1 = document.createElementNS("http://www.w3.org/2000/svg", "line");
        rodErr1.setAttribute("x1", 145); rodErr1.setAttribute("y1", 0);
        rodErr1.setAttribute("x2", 145); rodErr1.setAttribute("y2", 12);
        rodErr1.setAttribute("stroke", "#ff0055"); rodErr1.setAttribute("stroke-dasharray", "1,1");
        mainG.appendChild(rodErr1);
        
        const rodErr2 = document.createElementNS("http://www.w3.org/2000/svg", "line");
        rodErr2.setAttribute("x1", 145); rodErr2.setAttribute("y1", 70);
        rodErr2.setAttribute("x2", 145); rodErr2.setAttribute("y2", 82);
        rodErr2.setAttribute("stroke", "#ff0055"); rodErr2.setAttribute("stroke-dasharray", "1,1");
        mainG.appendChild(rodErr2);
      }

      drawBlock(139, 5, 151, 12, "bearing-body", "outer_plate");
      drawBlock(141, 5, 149, 12, "bearing-inner", "outer_plate");
      drawBlock(139, 70, 151, 77, "bearing-body", "inner_plate");
      drawBlock(141, 70, 149, 77, "bearing-inner", "inner_plate");

      drawBlock(145 - 14.5/2, 41, 145 + 14.5/2, 53, "jackshaft", "jackshaft_pinion");
      drawBlock(145 - 50.8/2, 55, 145 + 50.8/2, 67, "jackshaft", "jackshaft_pulley");
      drawBlock(141, 67, 149, 70, "collar-shim", "thrust_shim");

      // Motor (Dynamic size, orientation, and collision warning) centered at X=95
      let motorLen = 57;
      let motorRad = 18;
      if (state.designAMotor === "3536") {
        motorLen = 36;
        motorRad = 17.5;
      } else if (state.designAMotor === "3650") {
        motorLen = 50;
        motorRad = 18;
      }

      if (state.designAMount === "between") {
        // Motor face is at Z=55, extending outboard (towards Z=12)
        drawBlock(95 - 6.5, 55, 95 + 6.5, 67, "motor", "motor_body");
        drawBlock(95 - 13.5/2, 55, 95 + 13.5/2, 67, "motor", "motor_pinion");
        
        const motorStart = 55 - motorLen;
        if (motorStart >= 12) {
          drawBlock(95 - motorRad, motorStart, 95 + motorRad, 55, "motor", "motor_body");
        } else {
          drawBlock(95 - motorRad, 12, 95 + motorRad, 55, "motor", "motor_body");
          drawBlock(95 - motorRad, motorStart, 95 + motorRad, 12, "collision-warning", "motor_body");
        }
      } else {
        // Standard Inboard Mount: Motor face is at Z=82, body extends inboard
        drawBlock(95 - 6.5, 70, 95 + 6.5, 82, "motor", "motor_body");
        drawBlock(95 - motorRad, 82, 95 + motorRad, 82 + motorLen, "motor", "motor_body");
        drawBlock(95 - 13.5/2, 55, 95 + 13.5/2, 67, "motor", "motor_pinion");
      }

      if (state.showBelts) {
        const belt2 = document.createElementNS("http://www.w3.org/2000/svg", "rect");
        belt2.setAttribute("x", 137.75); belt2.setAttribute("y", 41 + 1);
        belt2.setAttribute("width", 63.6); belt2.setAttribute("height", 9);
        belt2.setAttribute("fill", "rgba(0, 255, 204, 0.15)"); belt2.setAttribute("stroke", "var(--color-belt2)");
        belt2.setAttribute("class", "interactive-element"); belt2.setAttribute("data-component-id", "belt_stage2");
        bindHoverEvents(belt2, "belt_stage2"); mainG.appendChild(belt2);

        const belt1 = document.createElementNS("http://www.w3.org/2000/svg", "rect");
        belt1.setAttribute("x", 88.25); belt1.setAttribute("y", 55 + 1);
        belt1.setAttribute("width", 82.15); belt1.setAttribute("height", 9);
        belt1.setAttribute("fill", "rgba(0, 255, 102, 0.15)"); belt1.setAttribute("stroke", "var(--color-belt1)");
        belt1.setAttribute("class", "interactive-element"); belt1.setAttribute("data-component-id", "belt_stage1");
        bindHoverEvents(belt1, "belt_stage1"); mainG.appendChild(belt1);
      }

      if (state.showClearances) {
        const insideMargin = document.createElementNS("http://www.w3.org/2000/svg", "rect");
        insideMargin.setAttribute("x", 0); insideMargin.setAttribute("y", 82);
        insideMargin.setAttribute("width", 215); insideMargin.setAttribute("height", 3);
        insideMargin.setAttribute("fill", "rgba(255, 0, 85, 0.05)"); insideMargin.setAttribute("stroke", "#ff0055");
        insideMargin.setAttribute("stroke-dasharray", "1, 1"); mainG.appendChild(insideMargin);
      }

      if (state.showDimensions) {
        mainG.appendChild(createDimLine(2.5, 0, 2.5, 12, 10, "12 mm Outer Plate", "z-width"));
        mainG.appendChild(createDimLine(175, 14, 175, 38, 15, "24 mm Wheel", "z-width"));
        mainG.appendChild(createDimLine(145, 41, 145, 53, 20, "12 mm Track 2", "z-width"));
        mainG.appendChild(createDimLine(145, 55, 145, 67, 20, "12 mm Track 1", "z-width"));
        mainG.appendChild(createDimLine(2.5, 70, 2.5, 82, 10, "12 mm Inner Plate", "z-width"));
        mainG.appendChild(createDimLine(212.5, 0, 212.5, 85, -15, "85 mm Envelope", "z-width"));
      }
    }

  } else if (state.currentDesign === "B") {
    // ---------------------------------
    // DESIGN B: DIRECT WORM DRIVE
    // ---------------------------------

    // Shifted Inner Plate (Z=65..77, leaving exactly 27mm space from Z=38)
    drawBlock(2.5, 65, 212.5, 77, "chassis-plate", "inner_plate");

    // Front Axle inner support bearing in shifted inner plate
    drawBlock(34, 65, 46, 72, "bearing-body", "inner_plate");
    drawBlock(36, 65, 44, 72, "bearing-inner", "inner_plate");

    // Rear Wheel (Z=14..38) and Spacing Washer
    drawBlock(172, 12, 178, 14, "spacing-washer", "spacing_washer");
    drawBlock(145, 14, 205, 38, "wheel", "rear_wheel");

    // Worm Gearbox Housing (Z=38..65 (27mm wide), X=137.1..195.1)
    drawBlock(137.1, 38, 195.1, 65, "gearbox", "worm_gearbox");

    // Motor can (Z=36..67 (Ø31), X=80.1..137.1)
    drawBlock(80.1, 36, 137.1, 67, "motor", "worm_motor");

    // Direct Drive Axle shaft (Extends 51.5mm from gearbox face Z=38 to Z=-13.5)
    const directShaft = document.createElementNS("http://www.w3.org/2000/svg", "line");
    directShaft.setAttribute("x1", 175); directShaft.setAttribute("y1", -13.5);
    directShaft.setAttribute("x2", 175); directShaft.setAttribute("y2", 38);
    directShaft.setAttribute("class", "interactive-element shaft-rod");
    directShaft.setAttribute("data-component-id", "direct_axle");
    directShaft.setAttribute("stroke-width", "2");
    bindHoverEvents(directShaft, "direct_axle");
    mainG.appendChild(directShaft);

    // Rear Bearings
    drawBlock(169, 5, 181, 12, "bearing-body", "outer_plate");
    drawBlock(171, 5, 179, 12, "bearing-inner", "outer_plate");

    if (state.showClearances) {
      // Draw 8mm wall clearance region
      const wallRegion = document.createElementNS("http://www.w3.org/2000/svg", "rect");
      wallRegion.setAttribute("x", 0); wallRegion.setAttribute("y", 77);
      wallRegion.setAttribute("width", 215); wallRegion.setAttribute("height", 8);
      wallRegion.setAttribute("fill", "rgba(0, 255, 102, 0.03)"); wallRegion.setAttribute("stroke", "#00ff66");
      wallRegion.setAttribute("stroke-opacity", "0.15"); wallRegion.setAttribute("stroke-dasharray", "1,1");
      mainG.appendChild(wallRegion);
    }

    if (state.showDimensions) {
      mainG.appendChild(createDimLine(2.5, 0, 2.5, 12, 10, "12 mm Outer Plate", "z-width"));
      mainG.appendChild(createDimLine(175, 14, 175, 38, 15, "24 mm Wheel", "z-width"));
      mainG.appendChild(createDimLine(145, 38, 145, 65, 20, "27 mm Worm Gearbox", "z-width"));
      mainG.appendChild(createDimLine(90, 38, 90, 69, 15, "31 mm Motor", "z-width"));
      mainG.appendChild(createDimLine(2.5, 65, 2.5, 77, 10, "12 mm Inner Plate", "z-width"));
      mainG.appendChild(createDimLine(5, 77, 5, 85, 8, "8 mm Wall Clearance", "z-width"));
      mainG.appendChild(createDimLine(212.5, 0, 212.5, 85, -15, "85 mm Envelope", "z-width"));
    }
  } else if (state.currentDesign === "C") {
    // ---------------------------------
    // DESIGN C: RAZOR HUB MOTOR & U-BRACKET (Single Centered Wheel)
    // ---------------------------------

    // Inner Plate (Z=71..83)
    drawBlock(2.5, 71, 212.5, 83, "chassis-plate", "inner_plate");

    // Razor Hub Wheel (Z=22.5..60.5 (38mm wide), X=107.5 - 50 to 107.5 + 50 (Ø100))
    drawBlock(107.5 - 50, 22.5, 107.5 + 50, 60.5, "wheel", "hub_motor");

    // Motor Hub Inner Casing (Z=20.0..63.0, X=107.5 - 40 to 107.5 + 40 (Ø80))
    drawBlock(107.5 - 40, 20.0, 107.5 + 40, 63.0, "motor", "hub_motor");

    // Axle Mounting Ears (10mm each side: Left ear Z=12.5..22.5, Right ear Z=60.5..70.5)
    // Left bracket arm: Z = 12.5..14.5, X = 90..125
    drawBlock(90, 12.5, 125, 14.5, "chassis-plate", "u_bracket");
    // Right bracket arm: Z = 68.5..70.5, X = 90..125
    drawBlock(90, 68.5, 125, 70.5, "chassis-plate", "u_bracket");

    // Axle shaft centerline passing through the center (X=107.5, Z=12.5..70.5 -> 58mm span)
    const hubAxle = document.createElementNS("http://www.w3.org/2000/svg", "line");
    hubAxle.setAttribute("x1", 107.5); hubAxle.setAttribute("y1", 12.5);
    hubAxle.setAttribute("x2", 107.5); hubAxle.setAttribute("y2", 70.5);
    hubAxle.setAttribute("class", "interactive-element shaft-rod");
    hubAxle.setAttribute("data-component-id", "hub_motor");
    hubAxle.setAttribute("stroke-width", "2.5");
    bindHoverEvents(hubAxle, "hub_motor");
    mainG.appendChild(hubAxle);

    if (state.showDimensions) {
      mainG.appendChild(createDimLine(2.5, 0, 2.5, 12, 10, "12 mm Outer Plate", "z-width"));
      mainG.appendChild(createDimLine(107.5, 22.5, 107.5, 60.5, 15, "38 mm Wheel Width", "z-width"));
      mainG.appendChild(createDimLine(107.5, 12.5, 107.5, 22.5, 25, "10 mm Ear", "z-width"));
      mainG.appendChild(createDimLine(107.5, 60.5, 107.5, 70.5, 25, "10 mm Ear", "z-width"));
      mainG.appendChild(createDimLine(107.5, 12.5, 107.5, 70.5, 35, "58 mm Total Axle Span", "z-width"));
      mainG.appendChild(createDimLine(2.5, 71, 2.5, 83, 10, "12 mm Inner Plate", "z-width"));
      mainG.appendChild(createDimLine(212.5, 0, 212.5, 85, -15, "85 mm Envelope", "z-width"));
    }
  }

  svg.appendChild(mainG);
}

// ----------------------------------------------------
// VIEW 3: END SECTIONAL VIEW (YZ-PLANE)
// ----------------------------------------------------
function drawEndView() {
  const svg = document.getElementById("svg-end-view");
  svg.innerHTML = "";
  setupMarkers(svg);

  svg.appendChild(drawGridAndAxes(svg, 85, 100, 85, 100));

  const mainG = document.createElementNS("http://www.w3.org/2000/svg", "g");

  function drawYZBlock(z1, y1, z2, y2, className, compId) {
    const r = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    r.setAttribute("x", Math.min(z1, z2));
    r.setAttribute("y", toSvgY(Math.max(y1, y2)));
    r.setAttribute("width", Math.abs(z2 - z1));
    r.setAttribute("height", Math.abs(y2 - y1));
    r.setAttribute("class", "interactive-element " + className);
    r.setAttribute("data-component-id", compId);
    bindHoverEvents(r, compId);
    mainG.appendChild(r);
    return r;
  }

  // Outer Plate (Z=0..12, Y=10..95)
  drawYZBlock(0, 10, 12, 95, "chassis-plate", "outer_plate");

  if (state.currentDesign === "A") {
    const motorY = state.designAStages === 1 ? 30 : 70;

    // Inner Plate (Z=70..82)
    drawYZBlock(70, 10, 82, 95, "chassis-plate", "inner_plate");
    
    // Wheel (Z=14..38, Y=0..60)
    drawYZBlock(14, 0, 38, 60, "wheel", "rear_wheel");

    if (state.designAStages === 1) {
      // 1-Stage direct belt wheel pulley
      drawYZBlock(55, 30 - 52.7/2, 67, 30 + 52.7/2, "wheel", "stage2_pulley");
    } else {
      // 2-Stage jackshaft wheel pulley and jackshaft elements
      drawYZBlock(41, 30 - 52.7/2, 53, 30 + 52.7/2, "wheel", "stage2_pulley");

      // Jackshaft rod (Z=12..70, Y=64..72)
      drawYZBlock(12, 64, 70, 72, "shaft-rod", "jackshaft_rod");
      drawYZBlock(41, 68 - 14.5/2, 53, 68 + 14.5/2, "jackshaft", "jackshaft_pinion");
      drawYZBlock(55, 68 - 50.8/2, 67, 68 + 50.8/2, "jackshaft", "jackshaft_pulley");
    }

    // Dynamic motor size based on selected Design A motor type
    let motorLen = 57;
    let motorRad = 18;
    if (state.designAMotor === "3536") {
      motorLen = 36;
      motorRad = 17.5;
    } else if (state.designAMotor === "3650") {
      motorLen = 50;
      motorRad = 18;
    }

    if (state.designAMount === "between") {
      const motorStart = 55 - motorLen;
      if (motorStart < 12) {
        // Collides!
        drawYZBlock(12, motorY - motorRad, 55, motorY + motorRad, "motor", "motor_body");
        drawYZBlock(motorStart, motorY - motorRad, 12, motorY + motorRad, "collision-warning", "motor_body");
      } else {
        // Fits perfectly
        drawYZBlock(motorStart, motorY - motorRad, 55, motorY + motorRad, "motor", "motor_body");
      }
      
      // Pinion
      drawYZBlock(55, motorY - 13.5/2, 67, motorY + 13.5/2, "motor", "motor_pinion");

      if (state.showLabels) {
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", 40); t.setAttribute("y", toSvgY(motorY) - (motorRad + 2));
        t.setAttribute("font-family", "var(--font-mono)");
        t.setAttribute("font-size", "4px"); t.setAttribute("text-anchor", "middle");
        if (motorStart < 12) {
          t.setAttribute("fill", "#ff0055");
          t.textContent = `Collides by ${Math.round(12 - motorStart)}mm!`;
        } else {
          t.setAttribute("fill", "#00ff66");
          t.textContent = "0mm Protrusion (Fits!)";
        }
        mainG.appendChild(t);
      }
    } else {
      const protrusion = (82 + motorLen) - 85; // 54, 33, or 47 mm

      // Motor can inside plate boundary (Z=82..85)
      drawYZBlock(82, motorY - motorRad, 85, motorY + motorRad, "motor", "motor_body");
      
      const motorExtend = document.createElementNS("http://www.w3.org/2000/svg", "rect");
      motorExtend.setAttribute("x", 85);
      motorExtend.setAttribute("y", toSvgY(motorY + motorRad));
      motorExtend.setAttribute("width", protrusion);
      motorExtend.setAttribute("height", motorRad * 2);
      motorExtend.setAttribute("fill", "none");
      motorExtend.setAttribute("stroke", "var(--color-motor)");
      motorExtend.setAttribute("stroke-width", "0.75");
      motorExtend.setAttribute("stroke-dasharray", "1, 1");
      mainG.appendChild(motorExtend);

      if (state.showClearances) {
        const redZone = document.createElementNS("http://www.w3.org/2000/svg", "rect");
        redZone.setAttribute("x", 85);
        redZone.setAttribute("y", toSvgY(100));
        redZone.setAttribute("width", protrusion);
        redZone.setAttribute("height", 100);
        redZone.setAttribute("fill", "rgba(255,0,85,0.08)");
        mainG.appendChild(redZone);
      }

      if (state.showLabels) {
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", 85 + protrusion / 2); t.setAttribute("y", toSvgY(motorY) - (motorRad + 2));
        t.setAttribute("fill", "#ff0055"); t.setAttribute("font-family", "var(--font-mono)");
        t.setAttribute("font-size", "4px"); t.setAttribute("text-anchor", "middle");
        t.textContent = `Protrudes ${protrusion}mm!`;
        mainG.appendChild(t);
      }
    }
  } else if (state.currentDesign === "B") {
    // Inner Plate (Z=65..77)
    drawYZBlock(65, 10, 77, 95, "chassis-plate", "inner_plate");

    // Wheel (Z=14..38, Y=0..60)
    drawYZBlock(14, 0, 38, 60, "wheel", "rear_wheel");

    // Gearbox Housing (Z=38..65, Y=10..50, centered at Y=30)
    drawYZBlock(38, 10, 65, 50, "gearbox", "worm_gearbox");

    // Motor cylinder (Z=36..67, Y=14.5..45.5, centered at Y=30)
    drawYZBlock(36, 14.5, 67, 45.5, "motor", "worm_motor");

    // Gearbox output bearing boss (Z=37..38, Y=24..36)
    drawYZBlock(37, 24, 38, 36, "bearing-body", "output_boss");

    // Direct Drive Axle (Z=5..38, Y=26..34)
    drawYZBlock(5, 26, 38, 34, "shaft-rod", "direct_axle");

    if (state.showClearances) {
      const greenZone = document.createElementNS("http://www.w3.org/2000/svg", "rect");
      greenZone.setAttribute("x", 77);
      greenZone.setAttribute("y", toSvgY(100));
      greenZone.setAttribute("width", 8);
      greenZone.setAttribute("height", 100);
      greenZone.setAttribute("fill", "rgba(0, 255, 102, 0.08)");
      mainG.appendChild(greenZone);
    }

    if (state.showLabels) {
      const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
      t.setAttribute("x", 81); t.setAttribute("y", toSvgY(70));
      t.setAttribute("fill", "#00ff66"); t.setAttribute("font-family", "var(--font-mono)");
      t.setAttribute("font-size", "3.5px"); t.setAttribute("text-anchor", "middle");
      t.textContent = "8mm gap";
      mainG.appendChild(t);
    }

    if (state.showDimensions) {
      mainG.appendChild(createDimLine(0, 95, 85, 95, -15, "85 mm Width Limit", "horizontal"));
      mainG.appendChild(createDimLine(85, 0, 85, 95, -5, "95 mm Timber Height", "vertical"));
    }
  } else if (state.currentDesign === "C") {
    // ---------------------------------
    // DESIGN C: RAZOR HUB MOTOR & U-BRACKET
    // ---------------------------------

    // Inner Plate (Z=71..83)
    drawYZBlock(71, 10, 83, 80, "chassis-plate", "inner_plate");

    // Razor Hub Wheel (Z=22.5..60.5 (38mm wide), Y=-12..88 (Ø100mm, 12mm ground clearance below Y=0))
    drawYZBlock(22.5, -12, 60.5, 88, "wheel", "hub_motor");

    // Motor Hub Inner Casing (Z=20.0..63.0, Y=-2..78 (Ø80mm))
    drawYZBlock(20.0, -2, 63.0, 78, "motor", "hub_motor");

    // Axle Mounting Ears (Z=12.5..70.5, 10mm ears each side, Y=34..42)
    drawYZBlock(12.5, 34, 70.5, 42, "shaft-rod", "hub_motor");

    // Suspended Axle Mount Bracket legs
    // Left leg: Z=12.5..14.5, Y=38..85
    drawYZBlock(12.5, 38, 14.5, 85, "chassis-plate", "u_bracket");
    // Right leg: Z=68.5..70.5, Y=38..85
    drawYZBlock(68.5, 38, 70.5, 85, "chassis-plate", "u_bracket");

    if (state.showClearances) {
      const greenZone = document.createElementNS("http://www.w3.org/2000/svg", "rect");
      greenZone.setAttribute("x", 83);
      greenZone.setAttribute("y", toSvgY(100));
      greenZone.setAttribute("width", 2);
      greenZone.setAttribute("height", 100);
      greenZone.setAttribute("fill", "rgba(0, 255, 102, 0.08)");
      mainG.appendChild(greenZone);
    }

    if (state.showLabels) {
      const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
      t.setAttribute("x", 84); t.setAttribute("y", toSvgY(70));
      t.setAttribute("fill", "#00ff66"); t.setAttribute("font-family", "var(--font-mono)");
      t.setAttribute("font-size", "3.5px"); t.setAttribute("text-anchor", "middle");
      t.textContent = "2mm gap";
      mainG.appendChild(t);
    }

    if (state.showDimensions) {
      mainG.appendChild(createDimLine(0, 95, 85, 95, -15, "85 mm Width Limit", "horizontal"));
      mainG.appendChild(createDimLine(85, 0, 85, 95, -5, "95 mm Timber Height", "vertical"));
    }
  }

  svg.appendChild(mainG);
}

// VIEW 4: BIRCH PLATE MACHINING VIEW (XY-PLANE OF PLATE)
// ----------------------------------------------------
function drawMachiningView() {
  const svg = document.getElementById("svg-machining-view");
  svg.innerHTML = "";
  setupMarkers(svg);

  const gGrid = document.createElementNS("http://www.w3.org/2000/svg", "g");
  if (state.showDimensions) {
    for (let x = 0; x <= 210; x += 10) {
      const l = document.createElementNS("http://www.w3.org/2000/svg", "line");
      l.setAttribute("x1", x); l.setAttribute("y1", 85);
      l.setAttribute("x2", x); l.setAttribute("y2", 0);
      l.setAttribute("class", x % 50 === 0 ? "grid-line-major" : "grid-line");
      gGrid.appendChild(l);
      if (x % 50 === 0 || x === 210) {
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", x); t.setAttribute("y", 85 + 7);
        t.setAttribute("class", "axis-text"); t.setAttribute("text-anchor", "middle");
        t.textContent = x; gGrid.appendChild(t);
      }
    }
    for (let y = 0; y <= 85; y += 10) {
      const l = document.createElementNS("http://www.w3.org/2000/svg", "line");
      l.setAttribute("x1", 0); l.setAttribute("y1", 85 - y);
      l.setAttribute("x2", 210); l.setAttribute("y2", 85 - y);
      l.setAttribute("class", y % 50 === 0 ? "grid-line-major" : "grid-line");
      gGrid.appendChild(l);
      const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
      t.setAttribute("x", -5); t.setAttribute("y", 85 - y + 1.5);
      t.setAttribute("class", "axis-text"); t.setAttribute("text-anchor", "end");
      t.textContent = y; gGrid.appendChild(t);
    }
  }

  const woodPlate = document.createElementNS("http://www.w3.org/2000/svg", "rect");
  woodPlate.setAttribute("x", 0); woodPlate.setAttribute("y", 0);
  woodPlate.setAttribute("width", 210); woodPlate.setAttribute("height", 85);
  woodPlate.setAttribute("fill", "#241c15"); woodPlate.setAttribute("stroke", "#b48b65");
  woodPlate.setAttribute("stroke-width", "1.25");
  gGrid.appendChild(woodPlate);
  svg.appendChild(gGrid);

  const mainG = document.createElementNS("http://www.w3.org/2000/svg", "g");

  function drawBearingBore(x, y, label, compId) {
    const bg = document.createElementNS("http://www.w3.org/2000/svg", "g");
    const pocket = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    pocket.setAttribute("cx", x); pocket.setAttribute("cy", 85 - y); pocket.setAttribute("r", 11);
    pocket.setAttribute("class", "forstner-pocket interactive-element");
    pocket.setAttribute("data-component-id", compId);
    bindHoverEvents(pocket, compId); bg.appendChild(pocket);

    const thru = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    thru.setAttribute("cx", x); thru.setAttribute("cy", 85 - y);
    thru.setAttribute("r", 5); // Ø10 thru
    thru.setAttribute("class", "through-hole interactive-element");
    thru.setAttribute("data-component-id", compId);
    bindHoverEvents(thru, compId); bg.appendChild(thru);

    const chH = document.createElementNS("http://www.w3.org/2000/svg", "line");
    chH.setAttribute("x1", x - 14); chH.setAttribute("y1", 85 - y);
    chH.setAttribute("x2", x + 14); chH.setAttribute("y2", 85 - y);
    chH.setAttribute("stroke", "rgba(255,255,255,0.2)"); chH.setAttribute("stroke-width", "0.5");
    bg.appendChild(chH);

    const chV = document.createElementNS("http://www.w3.org/2000/svg", "line");
    chV.setAttribute("x1", x); chV.setAttribute("y1", 85 - y - 14);
    chV.setAttribute("x2", x); chV.setAttribute("y2", 85 - y + 14);
    chV.setAttribute("stroke", "rgba(255,255,255,0.2)"); chV.setAttribute("stroke-width", "0.5");
    bg.appendChild(chV);

    if (state.showLabels) {
      const txt = document.createElementNS("http://www.w3.org/2000/svg", "text");
      txt.setAttribute("x", x); txt.setAttribute("y", 85 - y - 13);
      txt.setAttribute("fill", "#ff9d00"); txt.setAttribute("font-family", "var(--font-mono)");
      txt.setAttribute("font-size", "4px"); txt.setAttribute("text-anchor", "middle");
      txt.textContent = label; bg.appendChild(txt);
    }
    mainG.appendChild(bg);
  }

  // Front Axle (40, 20) - shifted down 10mm in board space
  if (state.currentDesign !== "C") {
    drawBearingBore(40, 20, "Front Bearing (40,20)", "front_wheel");
  }
  // Rear Axle (175, 20) - shifted down 10mm in board space
  if (state.currentDesign !== "C") {
    drawBearingBore(175, 20, "Rear Drive bearing (175,20)", "rear_wheel");
  }

  if (state.currentDesign === "A") {
    if (state.designAStages === 1) {
      // Motor Snout pilot (Ø13) centered at (107.5, 20) in board space
      const mPilot = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      mPilot.setAttribute("cx", 107.5); mPilot.setAttribute("cy", 85 - 20); mPilot.setAttribute("r", 6.5);
      mPilot.setAttribute("class", "through-hole interactive-element"); mPilot.setAttribute("data-component-id", "motor_body");
      bindHoverEvents(mPilot, "motor_body"); mainG.appendChild(mPilot);

      // Slots centered at (107.5, 20)
      const drawSlot = (centerX, centerY) => {
        const startX = centerX - 4; const endX = centerX + 4;
        const slotPath = `M ${startX} ${85 - (centerY - 1.75)} L ${endX} ${85 - (centerY - 1.75)} A 1.75 1.75 0 0 1 ${endX} ${85 - (centerY + 1.75)} L ${startX} ${85 - (centerY + 1.75)} A 1.75 1.75 0 0 1 ${startX} ${85 - (centerY - 1.75)} Z`;
        const sNode = document.createElementNS("http://www.w3.org/2000/svg", "path");
        sNode.setAttribute("d", slotPath); sNode.setAttribute("class", "through-hole interactive-element");
        sNode.setAttribute("data-component-id", "motor_body"); bindHoverEvents(sNode, "motor_body");
        mainG.appendChild(sNode);
      };
      drawSlot(95.0, 20);
      drawSlot(120.0, 20);

      if (state.showLabels) {
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", 107.5); t.setAttribute("y", 85 - 20 - 10);
        t.setAttribute("fill", "var(--color-motor)"); t.setAttribute("font-family", "var(--font-mono)");
        t.setAttribute("font-size", "4.5px"); t.setAttribute("text-anchor", "middle");
        t.textContent = "Ø13 Motor Pilot & M3 Slots";
        mainG.appendChild(t);
      }
    } else {
      // Jackshaft pocket (shifted down 10mm: 68 -> 58)
      drawBearingBore(145, 58, "Jackshaft bearing (145,58)", "jackshaft_rod");

      // Motor Snout pilot (Ø13) (shifted down 10mm: 70 -> 60)
      const mPilot = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      mPilot.setAttribute("cx", 95); mPilot.setAttribute("cy", 85 - 60); mPilot.setAttribute("r", 6.5);
      mPilot.setAttribute("class", "through-hole interactive-element"); mPilot.setAttribute("data-component-id", "motor_body");
      bindHoverEvents(mPilot, "motor_body"); mainG.appendChild(mPilot);

      // Slots centered at (95, 60)
      const drawSlot = (centerX, centerY) => {
        const startX = centerX - 4; const endX = centerX + 4;
        const slotPath = `M ${startX} ${85 - (centerY - 1.75)} L ${endX} ${85 - (centerY - 1.75)} A 1.75 1.75 0 0 1 ${endX} ${85 - (centerY + 1.75)} L ${startX} ${85 - (centerY + 1.75)} A 1.75 1.75 0 0 1 ${startX} ${85 - (centerY - 1.75)} Z`;
        const sNode = document.createElementNS("http://www.w3.org/2000/svg", "path");
        sNode.setAttribute("d", slotPath); sNode.setAttribute("class", "through-hole interactive-element");
        sNode.setAttribute("data-component-id", "motor_body"); bindHoverEvents(sNode, "motor_body");
        mainG.appendChild(sNode);
      };
      drawSlot(82.5, 60);
      drawSlot(107.5, 60);

      if (state.showLabels) {
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", 95); t.setAttribute("y", 85 - 60 - 10);
        t.setAttribute("fill", "var(--color-motor)"); t.setAttribute("font-family", "var(--font-mono)");
        t.setAttribute("font-size", "4.5px"); t.setAttribute("text-anchor", "middle");
        t.textContent = "Ø13 Motor Pilot & M3 Slots";
        mainG.appendChild(t);
      }
    }
  } else if (state.currentDesign === "B") {
    // Design B: Worm Gearbox mount holes (M4, Ø4.5 clearance holes)
    // Spaced 40mm horizontally (X), 28mm vertically (Y) based on actual 5840 drawing
    // Centered around axle (175, 20) with offsets:
    // X = 190.0 (axle + 15mm) and X = 150.0 (axle - 25mm)
    // Y = 6.0 (axle - 14mm) and Y = 34.0 (axle + 14mm)
    const mounts = [
      { x: 190.0, y: 34.0 },
      { x: 190.0, y: 6.0 },
      { x: 150.0, y: 34.0 },
      { x: 150.0, y: 6.0 }
    ];

    mounts.forEach(pt => {
      const h = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      h.setAttribute("cx", pt.x); h.setAttribute("cy", 85 - pt.y); h.setAttribute("r", 2.25); // Ø4.5 for M4 clearance
      h.setAttribute("class", "through-hole interactive-element");
      h.setAttribute("data-component-id", "worm_gearbox");
      bindHoverEvents(h, "worm_gearbox");
      mainG.appendChild(h);

      const cross1 = document.createElementNS("http://www.w3.org/2000/svg", "line");
      cross1.setAttribute("x1", pt.x - 4); cross1.setAttribute("y1", 85 - pt.y);
      cross1.setAttribute("x2", pt.x + 4); cross1.setAttribute("y2", 85 - pt.y);
      cross1.setAttribute("stroke", "rgba(255,255,255,0.2)"); cross1.setAttribute("stroke-width", "0.4");
      mainG.appendChild(cross1);

      const cross2 = document.createElementNS("http://www.w3.org/2000/svg", "line");
      cross2.setAttribute("x1", pt.x); cross2.setAttribute("y1", 85 - pt.y - 4);
      cross2.setAttribute("x2", pt.x); cross2.setAttribute("y2", 85 - pt.y + 4);
      cross2.setAttribute("stroke", "rgba(255,255,255,0.2)"); cross2.setAttribute("stroke-width", "0.4");
      mainG.appendChild(cross2);
    });

    if (state.showLabels) {
      const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
      t.setAttribute("x", 170); t.setAttribute("y", 85 - 34 - 4);
      t.setAttribute("fill", "var(--color-motor)"); t.setAttribute("font-family", "var(--font-mono)");
      t.setAttribute("font-size", "4px"); t.setAttribute("text-anchor", "middle");
      t.textContent = "4x Ø4.5 Gearbox Mounts (28x40 offset)";
      mainG.appendChild(t);
    }
  } else if (state.currentDesign === "C") {
    // Design C: No bearing bores required on the plywood side plates
    if (state.showLabels) {
      const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
      t.setAttribute("x", 107.5); t.setAttribute("y", 85 - 20 - 10);
      t.setAttribute("fill", "var(--color-motor)"); t.setAttribute("font-family", "var(--font-mono)");
      t.setAttribute("font-size", "4.5px"); t.setAttribute("text-anchor", "middle");
      t.textContent = "No Bearing Bores Required (Suspended Hub Motor)";
      mainG.appendChild(t);
    }
  }

  // Four standoffs (Corners at (15,15), (195,15), (15,75), (195,75)) - same in both, keeping 10mm margin from top edge
  const standoffs = [
    { x: 15, y: 15 }, { x: 195, y: 15 },
    { x: 15, y: 75 }, { x: 195, y: 75 }
  ];

  standoffs.forEach(pos => {
    const h = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    h.setAttribute("cx", pos.x); h.setAttribute("cy", 85 - pos.y); h.setAttribute("r", 2.25);
    h.setAttribute("class", "through-hole interactive-element");
    h.setAttribute("data-component-id", "outer_plate");
    bindHoverEvents(h, "outer_plate"); mainG.appendChild(h);
  });

  if (state.showDimensions) {
    mainG.appendChild(createDimLine(0, 0, 210, 0, -10, "210.0 mm Plate Width", "horizontal"));
    mainG.appendChild(createDimLine(0, 0, 0, 85, 10, "85.0 mm Plate Height", "vertical"));
    mainG.appendChild(createDimLine(0, 20, 40, 20, 15, "40.0", "horizontal"));
    mainG.appendChild(createDimLine(15, 0, 15, 15, 8, "15.0", "vertical"));
  }

  svg.appendChild(mainG);
}

// ----------------------------------------------------
// DYNAMIC CALCULATIONS & EVENT HANDLERS
// ----------------------------------------------------
function updateCalculations() {
  const rpmInput = document.getElementById("motor-rpm-input");
  state.motorRpm = parseInt(rpmInput.value);
  document.getElementById("motor-rpm-val").textContent = state.motorRpm.toLocaleString();

  let wheelRpm, ratioText;
  if (state.currentDesign === "A") {
    if (state.designAStages === 1) {
      const totalRatio = 4.0;
      wheelRpm = state.motorRpm / totalRatio;
      ratioText = "4.00 : 1";
      document.getElementById("stage1-ratio").innerHTML = '4.00 : 1 <span class="result-sub">(15T → 60T)</span>';
      document.getElementById("stage2-ratio").innerHTML = '1.00 : 1 <span class="result-sub">(Direct)</span>';
    } else {
      const totalRatio = (54 / 15) * (56 / 16); // 12.60
      wheelRpm = state.motorRpm / totalRatio;
      ratioText = "12.60 : 1";
      document.getElementById("stage1-ratio").innerHTML = '3.60 : 1 <span class="result-sub">(15T → 54T)</span>';
      document.getElementById("stage2-ratio").innerHTML = '3.50 : 1 <span class="result-sub">(16T → 56T)</span>';
    }
  } else if (state.currentDesign === "B") {
    // Worm Gearmotor reduction is dynamic from state
    const totalRatio = state.wormRatio;
    wheelRpm = state.motorRpm / totalRatio;
    ratioText = `${totalRatio.toFixed(2)} : 1`;
    document.getElementById("stage1-ratio").innerHTML = `${totalRatio.toFixed(2)} : 1 <span class="result-sub">(Worm Gear)</span>`;
    document.getElementById("stage2-ratio").innerHTML = '1.00 : 1 <span class="result-sub">(Direct)</span>';
  } else {
    // Design C: Hub Motor 1:1
    const totalRatio = 1.0;
    wheelRpm = state.motorRpm / totalRatio;
    ratioText = "1.00 : 1";
    document.getElementById("stage1-ratio").innerHTML = '1.00 : 1 <span class="result-sub">(Direct Hub)</span>';
    document.getElementById("stage2-ratio").innerHTML = '1.00 : 1 <span class="result-sub">(Direct Hub)</span>';
  }

  document.getElementById("total-ratio").textContent = ratioText;
  document.getElementById("wheel-rpm-val").textContent = Math.round(wheelRpm).toLocaleString() + " RPM";

  // Speed calculation (Ø98mm for C, Ø60mm for A/B)
  const wheelCircumference = Math.PI * (state.currentDesign === "C" ? 0.100 : 0.06);
  const speedMetersPerMinute = wheelRpm * wheelCircumference;
  const speedKph = (speedMetersPerMinute * 60) / 1000;
  const speedMph = speedKph * 0.621371;

  document.getElementById("speed-kph-val").textContent = speedKph.toFixed(1) + " km/h";
  document.getElementById("speed-mph-val").textContent = `(${speedMph.toFixed(1)} mph)`;

  // Torque demand calculation based on surface
  let tReq = 0.54; // default concrete
  if (state.surface === "carpet") {
    tReq = 2.18;
  } else if (state.surface === "ramp") {
    tReq = 4.46;
  }

  if (state.currentDesign === "C") {
    tReq *= 1.667; // 100mm wheel vs 60mm base wheel
  }

  let rated = 5.0;
  let stall = 15.0;

  if (state.currentDesign === "A") {
    if (state.designAMotor === "550") {
      rated = 6.4;
      stall = 48.0;
    } else if (state.designAMotor === "3536") {
      rated = 16.0;
      stall = 107.0;
    } else if (state.designAMotor === "3650") {
      rated = 21.0;
      stall = 128.0;
    }

    // Scale down output torque if using a single-stage reduction (4:1 vs 12.6:1)
    if (state.designAStages === 1) {
      rated = rated / 3.15;
      stall = stall / 3.15;
    }
  } else if (state.currentDesign === "B") {
    if (state.wormRatio === 17) {
      rated = 2.7;
      stall = 8.0;
    } else if (state.wormRatio === 31) {
      rated = 5.0;
      stall = 15.0;
    } else if (state.wormRatio === 50) {
      rated = 8.0;
      stall = 22.0;
    }
  } else {
    // Design C: Razor W13111701048 80W Hub Motor (continuous thermal torque is lower)
    rated = 4.0;
    stall = 12.0;
  }

  let statusText = "Safe (Continuous)";
  let statusColor = "#00ff66";

  if (tReq <= rated) {
    statusText = "Safe (Continuous)";
    statusColor = "#00ff66";
  } else if (tReq < stall) {
    statusText = "Warm (Short Bursts)";
    statusColor = "#ff9d00";
  } else {
    statusText = "STALL WARNING!";
    statusColor = "#ff0055";
  }

  // Override status if there is a physical frame collision
  if (state.currentDesign === "A" && state.designAMount === "between") {
    let motorLen = 57;
    if (state.designAMotor === "3536") motorLen = 36;
    else if (state.designAMotor === "3650") motorLen = 50;
    
    if (55 - motorLen < 12) {
      statusText = `COLLISION WARNING: Clipped ${Math.round(12 - (55 - motorLen))}mm!`;
      statusColor = "#ff0055";
    }
  }

  document.getElementById("torque-value").textContent = tReq.toFixed(2) + " kg·cm";
  const tStatus = document.getElementById("torque-status");
  tStatus.textContent = statusText;
  tStatus.style.color = statusColor;
}

function selectDesign(design) {
  state.currentDesign = design;
  
  const tabA = document.getElementById("tab-design-a");
  const tabB = document.getElementById("tab-design-b");
  const tabC = document.getElementById("tab-design-c");
  const summary = document.getElementById("design-summary");
  const calcInputs = document.querySelector(".calc-inputs label");
  const ratioRow = document.getElementById("ratio-selector-row");
  const stagesRow = document.getElementById("design-a-stages-row");
  const motorRow = document.getElementById("design-a-motor-row");
  const mountRow = document.getElementById("design-a-mount-row");
  const rpmInput = document.getElementById("motor-rpm-input");

  if (design === "A") {
    tabA.classList.add("active");
    tabB.classList.remove("active");
    tabC.classList.remove("active");
    summary.textContent = "Original Layout: Transverse motor driving an HTD-3M belt reduction. Protrusion depends on selected motor, mount style, and number of reduction stages.";
    calcInputs.textContent = "Motor Speed (RPM):";
    ratioRow.classList.add("hidden");
    stagesRow.classList.remove("hidden");
    motorRow.classList.remove("hidden");
    mountRow.classList.remove("hidden");
    
    // Set motor RPM defaults depending on motor type and stages
    if (state.designAStages === 1) {
      rpmInput.min = 1000;
      rpmInput.max = 5000;
      rpmInput.step = 100;
      rpmInput.value = 1800;
    } else {
      if (state.designAMotor === "550") {
        rpmInput.min = 5000;
        rpmInput.max = 25000;
        rpmInput.step = 500;
        rpmInput.value = 15000;
      } else if (state.designAMotor === "3536") {
        rpmInput.min = 4000;
        rpmInput.max = 20000;
        rpmInput.step = 500;
        rpmInput.value = 12000;
      } else if (state.designAMotor === "3650") {
        rpmInput.min = 5000;
        rpmInput.max = 25000;
        rpmInput.step = 500;
        rpmInput.value = 18000;
      }
    }
  } else if (design === "B") {
    tabA.classList.remove("active");
    tabB.classList.add("active");
    tabC.classList.remove("active");
    summary.textContent = "Space-Optimized Layout: Direct-drive 5840-341ZY Worm Gearmotor. Motor is rotated parallel to the wheels. Fits 100% inside Z=85 mm.";
    calcInputs.textContent = "Motor Rotor Speed (RPM):";
    ratioRow.classList.remove("hidden");
    stagesRow.classList.add("hidden");
    motorRow.classList.add("hidden");
    mountRow.classList.add("hidden");
    
    // Set 31ZY motor RPM defaults (factory max no-load is ~8000, 10000 allows for overvolting)
    rpmInput.min = 1000;
    rpmInput.max = 10000;
    rpmInput.step = 250;
    rpmInput.value = 6000;
  } else if (design === "C") {
    tabA.classList.remove("active");
    tabB.classList.remove("active");
    tabC.classList.add("active");
    summary.textContent = "Design C (Razor Hub Motor): A direct-drive sensored brushless hub motor with an integrated 100mm × 38mm wheel, 10mm mounting ears on each side (58mm total span), and 12mm ground clearance below the foot skirt.";
    calcInputs.textContent = "Hub Motor Speed (RPM):";
    ratioRow.classList.add("hidden");
    stagesRow.classList.add("hidden");
    motorRow.classList.add("hidden");
    mountRow.classList.add("hidden");

    // Set hub motor RPM defaults (low speed direct drive)
    rpmInput.min = 100;
    rpmInput.max = 1000;
    rpmInput.step = 50;
    rpmInput.value = 600;
  }

  state.motorRpm = parseInt(rpmInput.value);
  document.getElementById("motor-rpm-val").textContent = state.motorRpm.toLocaleString();

  drawAllViews();
  updateCalculations();
}

function initEvents() {
  // Option Toggles
  document.getElementById("toggle-dimensions").addEventListener("change", (e) => {
    state.showDimensions = e.target.checked;
    drawAllViews();
  });
  
  document.getElementById("toggle-clearances").addEventListener("change", (e) => {
    state.showClearances = e.target.checked;
    drawAllViews();
  });
  
  document.getElementById("toggle-belts").addEventListener("change", (e) => {
    state.showBelts = e.target.checked;
    drawAllViews();
  });
  
  document.getElementById("toggle-labels").addEventListener("change", (e) => {
    state.showLabels = e.target.checked;
    drawAllViews();
  });

  // Design Tabs
  document.getElementById("tab-design-a").addEventListener("click", () => selectDesign("A"));
  document.getElementById("tab-design-b").addEventListener("click", () => selectDesign("B"));
  document.getElementById("tab-design-c").addEventListener("click", () => selectDesign("C"));

  // Design A Reduction Stages Selection
  document.getElementById("design-a-stages-select").addEventListener("change", (e) => {
    state.designAStages = parseInt(e.target.value);
    
    const rpmInput = document.getElementById("motor-rpm-input");
    const valText = document.getElementById("motor-rpm-val");
    if (state.designAStages === 1) {
      rpmInput.min = 1000;
      rpmInput.max = 5000;
      rpmInput.step = 100;
      rpmInput.value = 1800;
    } else {
      if (state.designAMotor === "550") {
        rpmInput.min = 5000;
        rpmInput.max = 25000;
        rpmInput.step = 500;
        rpmInput.value = 15000;
      } else if (state.designAMotor === "3536") {
        rpmInput.min = 4000;
        rpmInput.max = 20000;
        rpmInput.step = 500;
        rpmInput.value = 12000;
      } else if (state.designAMotor === "3650") {
        rpmInput.min = 5000;
        rpmInput.max = 25000;
        rpmInput.step = 500;
        rpmInput.value = 18000;
      }
    }
    state.motorRpm = parseInt(rpmInput.value);
    valText.textContent = state.motorRpm.toLocaleString();
    
    updateCalculations();
    drawAllViews();
  });

  // Design A Motor Selection
  document.getElementById("design-a-motor-select").addEventListener("change", (e) => {
    state.designAMotor = e.target.value;
    
    const rpmInput = document.getElementById("motor-rpm-input");
    const valText = document.getElementById("motor-rpm-val");
    
    if (state.designAStages === 1) {
      // Keep low-KV speed ranges
      rpmInput.min = 1000;
      rpmInput.max = 5000;
      rpmInput.step = 100;
      rpmInput.value = 1800;
    } else {
      if (state.designAMotor === "550") {
        rpmInput.min = 5000;
        rpmInput.max = 25000;
        rpmInput.value = 15000;
      } else if (state.designAMotor === "3536") {
        rpmInput.min = 4000;
        rpmInput.max = 20000;
        rpmInput.value = 12000;
      } else if (state.designAMotor === "3650") {
        rpmInput.min = 5000;
        rpmInput.max = 25000;
        rpmInput.value = 18000;
      }
    }
    state.motorRpm = parseInt(rpmInput.value);
    valText.textContent = state.motorRpm.toLocaleString();
    
    updateCalculations();
    drawAllViews();
  });

  // Design A Mounting Style Selection
  document.getElementById("design-a-mount-select").addEventListener("change", (e) => {
    state.designAMount = e.target.value;
    updateCalculations();
    drawAllViews();
  });

  document.getElementById("worm-ratio-select").addEventListener("change", (e) => {
    state.wormRatio = parseInt(e.target.value);
    updateCalculations();
  });

  // Operating Surface Dropdown
  document.getElementById("surface-select").addEventListener("change", (e) => {
    state.surface = e.target.value;
    updateCalculations();
  });

  // Slider change
  document.getElementById("motor-rpm-input").addEventListener("input", updateCalculations);
}

function drawAllViews() {
  drawSideView();
  drawTopView();
  drawEndView();
  drawMachiningView();
}

// Entrypoint
window.addEventListener("DOMContentLoaded", () => {
  initEvents();
  drawAllViews();
  updateCalculations();
});
