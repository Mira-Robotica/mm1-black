"""Quaternion helpers, wxyz; angles follow src/lab/orientation.cpp."""
import math


def normalized(q):
    q = tuple(float(v) for v in q)
    if len(q) != 4 or not all(math.isfinite(v) for v in q):
        raise ValueError("Quaternion requires four finite components")
    norm = math.sqrt(sum(v*v for v in q))
    if abs(norm - 1) > 0.020001:
        raise ValueError("Quaternion norm outside schema 3 tolerance")
    return tuple(v / norm for v in q)


def rotation_error_deg(a, b):
    dot = abs(sum(x*y for x, y in zip(normalized(a), normalized(b))))
    if dot >= 1 - 1e-15:
        return 0.0
    return math.degrees(2 * math.acos(min(1.0, dot)))


def from_kortex_euler(theta):
    """Rz(theta_z) Ry(theta_y) Rx(theta_x), degrees, as in Kortex simulator."""
    x, y, z = (math.radians(v) / 2 for v in theta)
    cx, cy, cz = math.cos(x), math.cos(y), math.cos(z)
    sx, sy, sz = math.sin(x), math.sin(y), math.sin(z)
    return normalized((cx*cy*cz + sx*sy*sz, sx*cy*cz - cx*sy*sz,
                       cx*sy*cz + sx*cy*sz, cx*cy*sz - sx*sy*cz))


def angles(q, axis):
    w, x, y, z = normalized(q)
    axis = tuple(float(v) for v in axis)
    if len(axis) != 3 or not all(math.isfinite(v) for v in axis):
        raise ValueError("Invalid laser_axis in META")
    norm = math.sqrt(sum(v*v for v in axis))
    if norm < 1e-6:
        raise ValueError("Invalid laser_axis in META")
    bx, by, bz = (v/norm for v in axis)
    wx = (1-2*(y*y+z*z))*bx + 2*(x*y-w*z)*by + 2*(x*z+w*y)*bz
    wy = 2*(x*y+w*z)*bx + (1-2*(x*x+z*z))*by + 2*(y*z-w*x)*bz
    wz = 2*(x*z-w*y)*bx + 2*(y*z+w*x)*by + (1-2*(x*x+y*y))*bz
    horizontal = math.hypot(wx, wy)
    ry, rx = 2*(w*x+y*z), 1-2*(x*x+y*y)
    valid = horizontal >= 1e-5 and math.hypot(ry, rx) >= 1e-5
    return dict(quaternion_wxyz=[w, x, y, z], angles_valid=valid,
                angle_error="NONE" if valid else "ANGLE_SINGULARITY",
                azimuth_deg=math.degrees(math.atan2(wx, wy)) % 360 if horizontal >= 1e-5 else None,
                inclination_deg=math.degrees(math.atan2(wz, horizontal)),
                roll_deg=math.degrees(math.atan2(ry, rx)) if math.hypot(ry, rx) >= 1e-5 else None)


def analyze_capture(trena):
    """Keep raw reports separate; never infer an uncalibrated robot/IMU alignment."""
    sample = trena["capture"]["sample"]
    result = {"robot_imu_rotation_error_deg": None,
              "comparison_status": "FRAME_ALIGNMENT_NOT_CONFIGURED"}
    axis_text = trena["meta"].get("laser_axis")
    for name, flag, prefix in (("rv", "imu_valid", ""), ("grv", "grv_valid", "grv_")):
        if sample[flag] != "1":
            result[name] = {"quaternion_valid": False}
            continue
        q = normalized([sample[prefix + k] for k in ("qw", "qx", "qy", "qz")])
        result[name] = {"quaternion_valid": True, "quaternion_wxyz": q}
        if axis_text is not None:
            result[name].update(angles(q, axis_text.split("/")))
        else:
            result[name]["angle_error"] = "LASER_AXIS_NOT_ANNOUNCED"
    return result
