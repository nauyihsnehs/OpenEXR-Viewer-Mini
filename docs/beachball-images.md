# Beachball 人工核对清单

范围为 `singlepart.0001.exr`～`singlepart.0008.exr` 与 `multipart.0001.exr`～`multipart.0008.exr`，共 16 个文件。文件头已静态核对；以下画面和操作均待运行验收。本轮不编译、启动查看器或运行测试，不修改样例文件。

## 结构与眼别

- 逐文件打开，不增加序列切帧或播放。默认预览右眼 RGBA；菜单提供左右眼快捷切换及红青 Anaglyph 合成。
- 所有文件的 Display Window 为 `(0,0)`～`(2047,1555)`，像素宽高比为 1，全尺寸预览输出为 2048×1556；各 part 的 Data Window 可以不同。
- singlepart 有 20 个 HALF 通道，`multiView` 顺序为 `right, left`。无点号的 R/G/B/A/Z 属于默认 right；带视图名的通道按倒数第二段识别，例如 `forward.left.u` 属于 left。`disparityL.x/y` 与 `disparityR.x/y` 没有视图。
- multipart 有以下 10 个 part，眼别来自 `view` 属性，不从名称推断。表中序号从 0 开始。

| part | 源名称 | 源通道 | 标签 |
|---|---|---|---|
| 0 | rgba_right | R/G/B/A | right, default |
| 1 | depth_left | Z | left |
| 2 | forward_left | forward.u/v | left |
| 3 | whitebarmask_left | whitebarmask.mask | left |
| 4 | rgba_left | R/G/B/A | left |
| 5 | depth_right | Z | right, default |
| 6 | forward_right | forward.u/v | right, default |
| 7 | disparityL | disparityL.x/y | No view |
| 8 | disparityR | disparityR.x/y | No view |
| 9 | whitebarmask_right | whitebarmask.mask | right, default |

图层树、预览标题和极简窗口显示相同眼别信息。包含多个眼别的中间分组不加单一眼别标签。标签不改变源通道全名、part 索引或刷新使用的图层身份。RGBA 只在同一 part、同一眼别内合成；Z、运动向量、视差和 mask 使用现有单通道 Colormap。

## 待人工核对

| 检查项 | 操作与预期 |
|---|---|
| 每帧两种包装 | 对 1～8 帧分别打开 singlepart、multipart，默认均为右眼 RGBA。在相同显示参数下比较球体位置和构图。 |
| 左右眼 | 分别打开左右 RGBA，确认通道和眼别不同；在两个文件的共同源区域、同一文件坐标处核对原始数值。不要使用相同局部索引比较不同起点的 part。 |
| Z、forward、mask、disparity | 检查通道全名、眼别、源取值和 Auto 范围。任意数值数据不要求具有某种特定视觉外观；不能因为名称含 L/R 就给 disparity 补写眼别。 |
| 不同数据窗口 | mask 等小窗口位于正确的文件坐标；补空显示棋盘格，没有源读数。放大检查边界，切换图层后裁剪提示不能残留。 |
| 第 6～8 帧 | 依据 Attributes 的窗口范围核对右侧越界及裁剪图标；预览裁掉越界数据，活动图层与整文件原始导出仍保留完整 Data Window。 |
| 刷新与极简窗口 | 保持当前眼别、通道、预览参数、异常开关以及普通窗口缩放和平移；极简底栏可辨认眼别，过窄时悬停查看完整提示。 |
| 复制与预览输出 | 全尺寸和限宽复制、PNG 保持透明补空；JPEG 黑／白背景选择仍有效。使用统一黑底比较同眼画面，标记在最终输出尺寸绘制。 |

样例 README 只提到第 7、8 帧越界，但实际第 6 帧已有数据越过右边界：左右眼颜色 Data Window 的最大 x 分别为 2061、2095，大于 Display Window 的 2047。应以每个 part 的实际头信息为准。

singlepart 的统一数据窗口内可能显式存有零值，multipart 的某些 part 在对应位置没有数据。前者可显示为黑色，后者显示棋盘格；这不表示读取错误。比较源值只选双方都有源样本的区域；比较构图可将预览 JPEG 都保存到黑底。参考图只用于构图与可见内容比较，不作为逐像素色彩基准。

## 原始导出

- 使用 FLOAT＋ZIP 做精确往返核对。活动图层保留原始通道全名和该 part 的两个窗口；singlepart 活动图层的 Basic 导出仍保留完整 `multiView` 原顺序，即使只选中 left 通道。
- 整文件选择 Preserve multipart：保留源 part 顺序、名称、各自窗口及通道，输出为扫描线。Basic 保留源 `view/multiView`、色域和像素宽高比；None 可去除这些可选元数据。原本没有 `view` 的 disparity part 不补写该属性。
- Color channels (RGB/YC) 只保留左右颜色 part，次序仍为原 part 0、4；不把无颜色 part 回退成全部通道。活动 Z/forward/mask/disparity 使用该筛选时明确失败；整文件筛选全部为空也明确失败。
- 多视图 multipart 请求 Flatten 时明确提示使用 Preserve multipart，包括 Metadata None。单 part 不添加人为 part 前缀。普通非多视图 multipart 只有完整 Data Window、Display Window、像素比例等可合并时才允许扁平化；尺寸相同但起点不同仍必须拒绝。
- 导出后重新打开，逐 part 核对眼别、原始通道名、源值、窗口和裁剪外像素。None 去除视图说明后不再保证自动识别原眼别，这是该选项的预期行为。

## Stereo 菜单与红青预览（待运行验收）

- 入口为 View → Show → Stereo：Default、Left eye only、Right eye only、Anaglyph 3D。每个文件独立记忆，程序会话之外不保存；新文件从 Default 开始。点击 Default 回到文件默认图层，手动打开源图层则退出快捷模式；切回 Anaglyph 标签页应重新勾选 Anaglyph。
- 左右眼快捷项不筛掉树节点，不关闭其他标签页。重复选择复用已有页面；每个文件至多有一个默认颜色层眼对的 Anaglyph 页面。
- 对 16 个 Beachball 文件检查菜单可用性、左右眼及合成画面。额外夹具检查缺眼、重复候选、不同图层路径、Display Window 或像素比例不一致；不可用项禁用并通过悬停说明原因。不从 part 名或视差通道名猜眼别。
- 固定左红右青：两眼各自完成曝光、色调映射或伪彩色后，输出取左眼 R、右眼 G/B。新建 Anaglyph 继承当前颜色页面参数，之后独立保存，并将同一套参数用于两眼。使用左红右青眼镜核对定位和立体感；本版不使用优化矩阵，不提供眼位交换。
- Data Window 不同时按文件坐标对齐。仅一眼有数据时另一眼的贡献为零；两眼都缺失时为透明，界面露出棋盘格，无源读数。源 Alpha 不再次衰减颜色。继续检查第 6～8 帧越界裁剪、两眼范围并集以及小窗口之间的透明空隙。
- 悬停应分别列出 Left/Right 的真实源通道值及采样坐标，缺失眼注明 no data。双眼统计只汇总真实源通道样本，不统计合成分量；Auto 使用两眼共同亮度范围。
- 开启异常标记后，两眼的源异常均可定位；标记不进入双方均无数据的空隙。缩放、像素比例及复制／导出的标记大小继续沿用现有规则。
- 全尺寸和限宽复制、PNG/JPEG 预览输出与页面一致；PNG/复制保留透明补空，JPEG 按黑／白选项合成。Anaglyph 页面禁用 Active Original、HDR 和 HDR Bracketed Images；Layered Original 仍导出整个源文件，不导出红青混合值。查看任一源眼图层后恢复原始／HDR 导出能力。
- 刷新保留眼别、合成参数、异常开关和窗口位置；配对变化、某眼缺失或读取失败时保留旧文件及旧画面。加载过程中切换回 Default、关闭文件或重新选择，不应出现过期合成画面。极简底栏应注明 Anaglyph 3D — Left red / Right cyan。

## 静态审查与测试边界

已补充未执行的回归测试代码：空 multiView、深层通道名、无视图通道、误导性 part 名、默认眼颜色优先、10 part 与等价 singlepart 的源值和坐标、同源并发读取、取消和缺失数据、刷新眼别恢复、复制与极简窗口、Basic/None 导出、颜色筛选空结果及扁平化拒绝。

静态审查包括共享视图解析、图层身份不变、各 part 缓冲与扫描线范围、互斥锁绑定 framebuffer 和读取的生命周期、异步发布、源值与预览输出消费者，以及复制、极简窗口和状态恢复路径。执行补丁格式检查；这不替代 16 个样例的实际运行验收。

Stereo 新增测试代码覆盖已知颜色组合、三种显示模式、YA/YC、负起点与不同数据窗口、透明空隙、源 Alpha、双眼统计和异常坐标、配对缺失及歧义、异步取消／缺失数据、菜单、刷新保留与失败回退、标签页复用、复制、极简窗口和导出限制。测试均未执行；不据此宣称红青立体效果或性能已经通过运行验收。
