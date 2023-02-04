// SPDX-License-Identifier: BSD-3-Clause
// mrv2 (mrv22)
// Copyright Contributors to the mrv2 Project. All rights reserved.


//
// The original shader is an open source RV shader that maps a latlong
// image into a virtual sphere.
//
// Its original copyright was The Mill. 
//
//
#version 410

in vec3 vPos;
in vec2 vTexture;
uniform vec2 Size0;
uniform float vAperture;
uniform float hAperture;
uniform float focalLength;
uniform float rotateX;
uniform float rotateY;
out vec2 fTexture;

uniform struct Transform
{
    mat4 mvp;
} transform;

const float PI = 3.141592654;
const float PI2 = PI*2.0;
const float DEG_TO_RAD = PI/180.0;
const float ONE_OVERPI = 1.0/PI;
const float ONE_OVERPI2 = 1.0/PI2;

//lat long to xyz
vec3 LatToXYZ(vec2 p)
{
    float s_theta = sin(p.x);
    float c_theta = cos(p.x);
    float s_phi   = sin(p.y);
    float c_phi   = cos(p.y);
    return vec3(s_theta*c_phi, -c_theta, s_theta*s_phi);
}

//xyz to spherical(lat long)
vec2 XYZToLat(vec3 p)
{
    vec2 sph;
    p.y = clamp(p.y, -1.0, 1.0);
    sph.x = acos(-p.y);
    sph.y = atan(p.z,p.x);

    if(sph.y < 0.0)
	sph.y = sph.y + PI2;


    return sph;
}


void main ()
{
    gl_Position = transform.mvp * vec4(vPos, 1.0);

    
    vec2 size = Size0;
    float vAper = vAperture;
    if (vAper == 0.0)
	vAper = hAperture * (size.y/size.x);
    float aspect = vAper/hAperture;

    // find location relative to center
    vec2 p = (vTexture - 0.5) * size;
    // fTexture = p / size;  // OK
    // return;

    // convert to physical coordiantes
    p = p * vec2(hAperture*aspect, vAper)*(1.0/(size.y));  // OK
    // fTexture = p; // OK
    // return;

    if(abs(p.x) > hAperture*0.5 || abs(p.y) > vAper*0.5) // OK
    {
        fTexture = vec2(0.0,0.0); // OK
        return;
    }

    vec2 viewDir = vec2(clamp(rotateX*DEG_TO_RAD, 0.0001, PI - 0.0001),
                        rotateY*DEG_TO_RAD);

    vec3 view = LatToXYZ(viewDir);
    // view.y = 0.309804  // we get 0.31412
    fTexture = vec2( view.x, view.y );  // BAD
    return;
    
    vec3 up = normalize(vec3(0.0,1.0,0.0) - view.y*view);
    fTexture = vec2( up.x, up.y );  // BAD
    // return;
    
    vec3 right = cross(view, up);
    fTexture = vec2( right.x, right.y );  // BAD
    // return;

    view = view * focalLength;
    fTexture = vec2(view.x, view.y);  // OK
    // return;
    
    view += right * p.x;
    fTexture = vec2(view.x, view.y);  // BAD
    // return;
    
    view += up * p.y;
    fTexture = vec2(view.x, view.y);  // mostly OK - has an offset
    // return;
    
    view = normalize(view);
    fTexture = vec2(view.x, view.y);  // BAD
    return;
    
    vec2 sph = XYZToLat(view);
    fTexture = sph;  // BAD
    // return;
    
    sph = vec2(sph.y * ONE_OVERPI2, sph.x * ONE_OVERPI);
    fTexture = sph;   // OK
    // return;

    sph -= vTexture;  // BAD
    fTexture = sph;


}
