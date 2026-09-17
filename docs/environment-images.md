# 环境图人工核对清单

本轮覆盖 `OpenEXR Test Images/MultiResolution/` 的 8 张环境图；均为 **待运行验收**。
只做静态审查，未编译、启动查看器或执行测试。EXR、参考 JPG 和样例目录保持原样。
JPG 只用于识别内容，不作为逐像素色彩、接缝或投影比例基准。

| 源样例 | 第 0 层尺寸 | 重点 |
| --- | --- | --- |
| OrientationLatLong | 1024×512 | +Z 居中，+Y 在上；环视时文字方向、左右及上下不翻转 |
| OrientationCube | 512×3072 | +X、−X、+Y、−Y、+Z、−Z 六面竖条，逐面方向与 LatLong 对应 |
| WavyLinesLatLong | 1024×512 | 经度接缝、两极、环视及球面上的线条连续性 |
| WavyLinesCube | 256×1536 | 六面交界及三面公共顶点的插值，无串入竖条中不相邻的面 |
| StageEnvLatLong | 1000×500 | 棚内环视、亮灯及天花板/地板；ROUND_UP 层尺寸 |
| StageEnvCube | 256×1536 | 对应方向的棚内构图；不同源分辨率允许局部差异 |
| KernerEnvLatLong | 1024×512 | 全景内容、背面可见性、曝光与明暗细节 |
| KernerEnvCube | 256×1536 | 与 LatLong 同向对照；环视后切回源布局仍完整 |

## 源、显示与导出

- 初次打开按 `envmap` 的源投影显示，信息栏区分 Source 与 Display。
- `View → Show → Projection` 的四个选项互斥；普通图像禁用。独立 R/G/B/A 等仍使用 Colormap。
- LatLong/Cube 保留平移、缩放、Fit；Cube 采用 OpenEXR 标准竖条而非十字展开。
- Pers-view 左拖改变方向，普通滚轮调水平 FOV；默认 +Z、+Y 上方、90°，限制 10°～150°。
- Sphere 是从外部看的正交贴图球，无额外光照或反射；左拖旋转，滚轮缩放，球外棋盘格。
- 两种相机视图共用朝向。投影菜单 Reset 恢复方向和 FOV；右键颜色重置不改变这些值。
- 中键保留普通平移/极简窗口移动。检查明暗主题、高 DPI、极简窗口进出后状态。
- 连续快速转动、切换投影和颜色参数，只显示最新完整结果；画面、读数、球外覆盖和标记同步。
- 转到背面再返回源布局，确认内容没有遗失。源统计与 Auto 不应随视角改变。

## 读数、层级与异常

- 原生布局的读数保留真实源通道名、文件坐标与精确源值。
- 转换视图标为 `Interpolated linear`，列出源采样文件坐标；颜色值在曝光/色调映射之前，单通道保留原名。
- 球外清空读数。异常标记仅来自实际参与采样的源值，不把插值传播计入源统计。
- 检查 NaN/±Inf 的定位、边界标记和球外裁剪；合法负值不标为异常。
- 逐层切换必须读取存储层；退化 LatLong 的单行/单列不出现除零或无效坐标。
- 512 面宽 Cube 到 `(9,9)` 是 1×6，256 面宽到 `(8,8)` 是 1×6。
  再往后的 1×3、1×1 不足六面，只能原生查看与原始导出；信息栏应说明原因。
- 刷新及切层后保留当前投影和朝向（不完整 Cube 层退回原生平面）；Fit 重新适应，手动缩放保留倍率与相对中心。
- 投影转换使用完整源数据，不套用原生 Display Window 裁剪；转换视图不显示原生裁剪图标。
- 已有 `WavyLinesSphere.exr` 没有完整背面，仍按普通图像查看，不作为本轮环境源。

## 保存与复制

- `Projection Conversion` 的初始投影、朝向与已显示结果相同；修改保存参数不改变页面。
- LatLong 固定 2:1；Cube 宽度即单面边长，高度为六倍；Sphere 正方形；Pers-view 可独立改宽高和水平 FOV。
- 检查默认尺寸：Cube 面宽 N，LatLong 源 `max(1, round(width/4))` 为 N；输出分别 4N×2N、N×6N、2N×2N。
- PNG 球外透明；JPEG 黑/白背景均可选。普通预览保存、全尺寸/限宽复制使用当前显示投影。
- 标记在最终输出尺寸绘制，直径保持 12 输出像素；原始与转换 EXR 不含标记。
- 转换 EXR 默认 FLOAT＋ZIP，单 part 扫描线；RGBA 的源 Alpha 保留，RGB/标量必要时加覆盖 Alpha。
  独立通道名为 A 时，Sphere 的额外覆盖通道名为 `coverage.A`，避免覆盖原 A 值。
- EXR 颜色为未曝光、未色调映射的线性 Rec.709 RGB/RGBA；独立通道无色带映射。
- 转换输出窗口起点 `(0,0)`、像素比例 1；LatLong/Cube 写目标 `envmap`，Pers-view/Sphere 不写。
  即使色带、曝光、高亮开启，也不改变转换 EXR 的线性值。
- 活动图层及整文件原始 EXR 仍保留当前源层的全部数值、布局、窗口；Basic 保留源 `envmap`，None 可去除。
- HDR/曝光序列保持源层线性数据导出，不能把投影后的画布误当源缓冲。
- 保存对话框打开后即使之前的渲染请求完成，转换/预览导出仍使用打开时已提交的源、层级及显示参数。

## 静态验证范围

- 已补未执行的回归用例：六方向及面角、负起点、视场角、Sphere 透明度、退化层、异常分类和取消；
  源缓冲不变、线性 EXR/色域/envmap、PNG/JPEG、标量名称与覆盖 Alpha、菜单/刷新/极简恢复。
- 审查异步任务按值捕获、最新请求提交、原始/显示缓冲消费者、原始元数据传递和最终尺寸标记路径。
- 运行验收及 8 张实际样例的视觉质量、交互速度、不同 DPI 下的体验仍需人工核对。

方向及边界约定依据 [OpenEXR 环境图说明](https://openexr.com/en/latest/ReadingAndWritingImageFiles.html#environment-maps)
和 [ImfEnvmap 接口](https://github.com/AcademySoftwareFoundation/openexr/blob/main/src/lib/OpenEXR/ImfEnvmap.h)。
