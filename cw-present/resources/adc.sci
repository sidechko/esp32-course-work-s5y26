function d = get_divisors(n)
    k = 1:int(sqrt(n));
    c = n ./ k;
    idx = find(c == int(c));
    divs = unique([k(idx), n ./ k(idx)]);
    d = gsort(divs, 'g', 'i');
endfunction

step = 4096
xm = 0
xM = 3.3
xD = 3.3 / step
ym = 0
yM = step
yD = step / step

x = xm:xD:xM
y = ym:yD:yM

s0 = int(1.08 / xD)
s1 = int(2.08 / xD)
_step = 29
ds = int((s1-s0)/_step)
xDS = x(s0):xD:x(s1)
yDS = y(s0):yD:y(s1)
yDS = int(yDS ./ ds).*ds
xDS(1)
yDS(1)
xDS($)
yDS($)

yDS($) - yDS(1)
disp(get_divisors(yDS($) - yDS(1)));
plot(x,y,x(s0),y(s0),'ro',x(s1),y(s1),'ro',xDS,yDS)