#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "maxps4_backend.h"
#include "maxps4_core.h"
#include <cmath>

@interface MaxPS4IntegratedViewController : UIViewController <UIDocumentPickerDelegate, MTKViewDelegate>
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UILabel *guestOutputLabel;
@property(nonatomic, strong) UILabel *stateValueLabel;
@property(nonatomic, strong) UILabel *fileValueLabel;
@property(nonatomic, strong) UILabel *controllerValueLabel;
@property(nonatomic, strong) UILabel *metalValueLabel;
@property(nonatomic, strong) UILabel *controllerInputLabel;
@property(nonatomic, strong) MTKView *metalProbeView;
@property(nonatomic, strong) id<MTLCommandQueue> metalCommandQueue;
@property(nonatomic, strong) id<MTLRenderPipelineState> metalProbePipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> metalTexturePipeline;
@property(nonatomic, strong) id<MTLTexture> metalProbeTexture;
@property(nonatomic, strong) NSMutableDictionary *guestUploadedTextures;
@property(nonatomic, strong) NSMutableDictionary *guestUploadedTextureSequences;
@property(nonatomic, assign) float probeX;
@property(nonatomic, assign) float probeY;
@property(nonatomic, assign) float probeScale;
@property(nonatomic, assign) unsigned int probeButtons;
@property(nonatomic, strong) UIButton *testButton;
@property(nonatomic, strong) UIButton *multithreadTestButton;
@property(nonatomic, strong) UIButton *importButton;
@property(nonatomic, strong) UIButton *diagnosticCopyButton;
@property(nonatomic, strong) NSTimer *diagnosticTimer;
@property(nonatomic, strong) NSDate *bootStartedAt;
@property(nonatomic, strong) NSURL *selectedURL;
@property(nonatomic, copy) NSString *lastDiagnostic;
@property(nonatomic, assign) unsigned long long selectedFileSize;
@property(nonatomic, assign) BOOL bootInProgress;
@property(nonatomic, assign) NSUInteger bootGeneration;
@property(nonatomic, assign) NSUInteger copyFeedbackGeneration;
@property(nonatomic, strong) UISegmentedControl *libraryCategoryControl;
@property(nonatomic, strong) UIStackView *libraryStack;
@property(nonatomic, strong) UILabel *libraryEmptyLabel;
@property(nonatomic, strong) NSMutableArray<NSMutableDictionary *> *libraryGames;
@property(nonatomic, strong) NSArray<NSMutableDictionary *> *visibleLibraryGames;
- (void)loadLibrary;
- (void)saveLibrary;
- (void)refreshLibraryUI;
- (NSURL *)persistImportedExecutable:(NSURL *)url;
- (void)launchManagedExecutableAtPath:(NSString *)path
                          displayName:(NSString *)displayName
                            fileSize:(unsigned long long)fileSize;
@end

@implementation MaxPS4IntegratedViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    self.view.backgroundColor = [UIColor colorWithRed:0.018 green:0.022 blue:0.032 alpha:1.0];
    self.lastDiagnostic = @"Backend intégré. Active le JIT avec StikDebug, puis teste le backend ou importe ton propre eboot.bin / SELF.";
    [self loadLibrary];

    UIScrollView *scroll = [[UIScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.alwaysBounceVertical = YES;
    scroll.showsVerticalScrollIndicator = NO;

    UIView *content = [[UIView alloc] init];
    content.translatesAutoresizingMaskIntoConstraints = NO;

    [self.view addSubview:scroll];
    [scroll addSubview:content];

    UILabel *brand = [[UILabel alloc] init];
    brand.translatesAutoresizingMaskIntoConstraints = NO;
    brand.attributedText = [self brandText];
    brand.font = [UIFont systemFontOfSize:34.0 weight:UIFontWeightBlack];
    brand.accessibilityLabel = @"MaxPS4";

    UILabel *subtitle = [[UILabel alloc] init];
    subtitle.translatesAutoresizingMaskIntoConstraints = NO;
    subtitle.text = @"Ta bibliothèque PS4 sur iPhone";
    subtitle.textColor = [UIColor colorWithWhite:1.0 alpha:0.58];
    subtitle.font = [UIFont systemFontOfSize:15.0 weight:UIFontWeightMedium];

    UIView *backendCard = [self cardView];
    UIStackView *backendStack = [[UIStackView alloc] init];
    backendStack.translatesAutoresizingMaskIntoConstraints = NO;
    backendStack.axis = UILayoutConstraintAxisHorizontal;
    backendStack.alignment = UIStackViewAlignmentCenter;
    backendStack.spacing = 12.0;

    UIView *dot = [[UIView alloc] init];
    dot.translatesAutoresizingMaskIntoConstraints = NO;
    dot.backgroundColor = maxps4_core_jit_available()
        ? [UIColor colorWithRed:0.20 green:0.90 blue:0.45 alpha:1.0]
        : [UIColor colorWithRed:1.0 green:0.55 blue:0.12 alpha:1.0];
    dot.layer.cornerRadius = 7.0;
    dot.layer.shadowColor = dot.backgroundColor.CGColor;
    dot.layer.shadowRadius = 8.0;
    dot.layer.shadowOpacity = 0.8;
    dot.layer.shadowOffset = CGSizeMake(0.0, 0.0);

    UILabel *backendTitle = [[UILabel alloc] init];
    backendTitle.text = maxps4_core_jit_available() ? @"JIT actif • prêt à lancer" : @"JIT requis";
    backendTitle.textColor = UIColor.whiteColor;
    backendTitle.font = [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold];

    UILabel *backendSubtitle = [[UILabel alloc] init];
    backendSubtitle.text = maxps4_core_jit_available()
        ? @"StikDebug détecté • FEXCore disponible"
        : @"Active StikDebug avant de lancer un jeu";
    backendSubtitle.textColor = [UIColor colorWithWhite:1.0 alpha:0.56];
    backendSubtitle.font = [UIFont systemFontOfSize:13.0];

    UIStackView *backendText = [[UIStackView alloc] initWithArrangedSubviews:@[backendTitle, backendSubtitle]];
    backendText.axis = UILayoutConstraintAxisVertical;
    backendText.spacing = 3.0;

    UILabel *check = [[UILabel alloc] init];
    check.text = @"✓";
    check.textAlignment = NSTextAlignmentCenter;
    check.textColor = [UIColor colorWithRed:0.20 green:0.90 blue:0.45 alpha:1.0];
    check.font = [UIFont systemFontOfSize:24.0 weight:UIFontWeightBold];

    [backendStack addArrangedSubview:dot];
    [backendStack addArrangedSubview:backendText];
    [backendStack addArrangedSubview:check];
    [backendCard addSubview:backendStack];

    [NSLayoutConstraint activateConstraints:@[
        [dot.widthAnchor constraintEqualToConstant:14.0],
        [dot.heightAnchor constraintEqualToConstant:14.0],
        [backendStack.leadingAnchor constraintEqualToAnchor:backendCard.leadingAnchor constant:18.0],
        [backendStack.trailingAnchor constraintEqualToAnchor:backendCard.trailingAnchor constant:-18.0],
        [backendStack.topAnchor constraintEqualToAnchor:backendCard.topAnchor constant:16.0],
        [backendStack.bottomAnchor constraintEqualToAnchor:backendCard.bottomAnchor constant:-16.0],
    ]];

    self.importButton = [self actionButtonWithTitle:@"+  Ajouter un jeu"
                                          subtitle:@"Importer eboot.bin, SELF ou PKG autorisé"
                                           primary:YES];
    [self.importButton addTarget:self action:@selector(pickExecutable) forControlEvents:UIControlEventTouchUpInside];

    self.testButton = [self actionButtonWithTitle:@"Tester le backend shadPS4/FEX"
                                        subtitle:@"Vérifie le cœur intégré et son état"
                                         primary:NO];
    [self.testButton addTarget:self action:@selector(runBackendTest) forControlEvents:UIControlEventTouchUpInside];

    self.multithreadTestButton =
        [self actionButtonWithTitle:@"Tester MaxPS4Kernel multi-thread"
                           subtitle:@"Lance le test thr_new sur un second contexte FEX"
                            primary:NO];
    [self.multithreadTestButton addTarget:self
                                    action:@selector(runMultithreadKernelTest)
                          forControlEvents:UIControlEventTouchUpInside];

    UILabel *libraryTitle = [[UILabel alloc] init];
    libraryTitle.text = @"Mes jeux";
    libraryTitle.textColor = UIColor.whiteColor;
    libraryTitle.font = [UIFont systemFontOfSize:20.0 weight:UIFontWeightBold];

    self.libraryCategoryControl = [[UISegmentedControl alloc]
        initWithItems:@[@"Tous", @"Favoris", @"Récents", @"Homebrew"]];
    self.libraryCategoryControl.selectedSegmentIndex = 0;
    self.libraryCategoryControl.translatesAutoresizingMaskIntoConstraints = NO;
    self.libraryCategoryControl.selectedSegmentTintColor =
        [UIColor colorWithRed:0.14 green:0.42 blue:1.0 alpha:1.0];
    [self.libraryCategoryControl setTitleTextAttributes:@{
        NSForegroundColorAttributeName: [UIColor colorWithWhite:1.0 alpha:0.62],
        NSFontAttributeName: [UIFont systemFontOfSize:12.0 weight:UIFontWeightSemibold]
    } forState:UIControlStateNormal];
    [self.libraryCategoryControl setTitleTextAttributes:@{
        NSForegroundColorAttributeName: UIColor.whiteColor,
        NSFontAttributeName: [UIFont systemFontOfSize:12.0 weight:UIFontWeightBold]
    } forState:UIControlStateSelected];
    [self.libraryCategoryControl addTarget:self
                                    action:@selector(libraryCategoryChanged:)
                          forControlEvents:UIControlEventValueChanged];

    UIView *libraryCard = [self cardView];
    self.libraryStack = [[UIStackView alloc] init];
    self.libraryStack.translatesAutoresizingMaskIntoConstraints = NO;
    self.libraryStack.axis = UILayoutConstraintAxisVertical;
    self.libraryStack.spacing = 12.0;

    self.libraryEmptyLabel = [[UILabel alloc] init];
    self.libraryEmptyLabel.numberOfLines = 0;
    self.libraryEmptyLabel.textAlignment = NSTextAlignmentCenter;
    self.libraryEmptyLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.56];
    self.libraryEmptyLabel.font = [UIFont systemFontOfSize:14.0 weight:UIFontWeightMedium];

    [libraryCard addSubview:self.libraryStack];
    [NSLayoutConstraint activateConstraints:@[
        [self.libraryStack.leadingAnchor constraintEqualToAnchor:libraryCard.leadingAnchor constant:14.0],
        [self.libraryStack.trailingAnchor constraintEqualToAnchor:libraryCard.trailingAnchor constant:-14.0],
        [self.libraryStack.topAnchor constraintEqualToAnchor:libraryCard.topAnchor constant:14.0],
        [self.libraryStack.bottomAnchor constraintEqualToAnchor:libraryCard.bottomAnchor constant:-14.0],
    ]];

    [self refreshLibraryUI];

    UIView *ioCard = [self cardView];
    UILabel *ioTitle = [[UILabel alloc] init];
    ioTitle.translatesAutoresizingMaskIntoConstraints = NO;
    ioTitle.text = @"Entrée & rendu";
    ioTitle.textColor = UIColor.whiteColor;
    ioTitle.font = [UIFont systemFontOfSize:18.0 weight:UIFontWeightBold];

    UILabel *controllerLabel = [self mutedLabel:@"Manette"];
    self.controllerValueLabel = [self valueLabel:@"Recherche…"];
    UILabel *metalLabel = [self mutedLabel:@"Metal"];
    self.metalValueLabel = [self valueLabel:@"Détection…"];

    UIStackView *controllerRow = [self infoRowWithLeft:controllerLabel right:self.controllerValueLabel];
    UIStackView *metalRow = [self infoRowWithLeft:metalLabel right:self.metalValueLabel];

    self.controllerInputLabel = [[UILabel alloc] init];
    self.controllerInputLabel.numberOfLines = 0;
    self.controllerInputLabel.text = @"Entrées manette : en attente";
    self.controllerInputLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.58];
    self.controllerInputLabel.font = [UIFont monospacedSystemFontOfSize:12.0 weight:UIFontWeightRegular];

    id<MTLDevice> probeDevice = MTLCreateSystemDefaultDevice();
    self.metalProbeView = [[MTKView alloc] initWithFrame:CGRectZero device:probeDevice];
    self.metalProbeView.translatesAutoresizingMaskIntoConstraints = NO;
    self.metalProbeView.delegate = self;
    self.metalProbeView.paused = NO;
    self.metalProbeView.enableSetNeedsDisplay = NO;
    self.metalProbeView.preferredFramesPerSecond = 60;
    self.metalProbeView.clearColor = MTLClearColorMake(0.02, 0.24, 0.72, 1.0);
    self.metalProbeView.layer.cornerRadius = 14.0;
    self.metalProbeView.layer.masksToBounds = YES;
    self.metalCommandQueue = [probeDevice newCommandQueue];
    self.probeX = 0.0f;
    self.probeY = 0.0f;
    self.probeScale = 1.0f;
    self.probeButtons = 0;
    self.guestUploadedTextures = [NSMutableDictionary dictionary];
    self.guestUploadedTextureSequences = [NSMutableDictionary dictionary];

    if (probeDevice) {
        NSString *shaderSource =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct VOut { float4 position [[position]]; };\n"
             "vertex VOut maxps4_probe_vertex(const device float2 *v [[buffer(0)]], uint id [[vertex_id]]) {\n"
             "  VOut o; o.position = float4(v[id], 0.0, 1.0); return o;\n"
             "}\n"
             "fragment float4 maxps4_probe_fragment(constant float4 &color [[buffer(0)]]) { return color; }\n"
             "struct VTexOut { float4 position [[position]]; float2 uv; };\n"
             "vertex VTexOut maxps4_texture_vertex(const device float4 *v [[buffer(0)]], uint id [[vertex_id]]) {\n"
             "  VTexOut o; o.position = float4(v[id].xy, 0.0, 1.0); o.uv = v[id].zw; return o;\n"
             "}\n"
             "fragment float4 maxps4_texture_fragment(VTexOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {\n"
             "  constexpr sampler s(address::repeat, filter::nearest); return tex.sample(s, in.uv);\n"
             "}\n";
        NSError *libraryError = nil;
        id<MTLLibrary> library = [probeDevice newLibraryWithSource:shaderSource options:nil error:&libraryError];
        if (library) {
            MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction = [library newFunctionWithName:@"maxps4_probe_vertex"];
            desc.fragmentFunction = [library newFunctionWithName:@"maxps4_probe_fragment"];
            desc.colorAttachments[0].pixelFormat = self.metalProbeView.colorPixelFormat;
            NSError *pipelineError = nil;
            self.metalProbePipeline = [probeDevice newRenderPipelineStateWithDescriptor:desc error:&pipelineError];

            MTLRenderPipelineDescriptor *textureDesc = [[MTLRenderPipelineDescriptor alloc] init];
            textureDesc.vertexFunction = [library newFunctionWithName:@"maxps4_texture_vertex"];
            textureDesc.fragmentFunction = [library newFunctionWithName:@"maxps4_texture_fragment"];
            textureDesc.colorAttachments[0].pixelFormat = self.metalProbeView.colorPixelFormat;
            NSError *texturePipelineError = nil;
            self.metalTexturePipeline =
                [probeDevice newRenderPipelineStateWithDescriptor:textureDesc
                                                            error:&texturePipelineError];

            // Small procedurally generated 8x8 checker texture. It contains no
            // external/proprietary image data and exists only for the legal
            // guest-to-Metal texture smoke test.
            MTLTextureDescriptor *textureDescriptor =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                   width:8
                                                                  height:8
                                                               mipmapped:NO];
            textureDescriptor.usage = MTLTextureUsageShaderRead;
            self.metalProbeTexture = [probeDevice newTextureWithDescriptor:textureDescriptor];
            if (self.metalProbeTexture) {
                unsigned char pixels[8 * 8 * 4];
                for (NSUInteger y = 0; y < 8; ++y) {
                    for (NSUInteger x = 0; x < 8; ++x) {
                        const BOOL bright = (((x / 2) + (y / 2)) & 1) == 0;
                        const NSUInteger p = (y * 8 + x) * 4;
                        pixels[p + 0] = bright ? 35 : 255;
                        pixels[p + 1] = bright ? 220 : 65;
                        pixels[p + 2] = bright ? 255 : 180;
                        pixels[p + 3] = 255;
                    }
                }
                [self.metalProbeTexture replaceRegion:MTLRegionMake2D(0, 0, 8, 8)
                                         mipmapLevel:0
                                           withBytes:pixels
                                         bytesPerRow:8 * 4];
            }

            (void)pipelineError;
            (void)texturePipelineError;
        }
        (void)libraryError;
    }
    [self.metalProbeView.heightAnchor constraintEqualToConstant:110.0].active = YES;

    UIStackView *ioStack = [[UIStackView alloc] initWithArrangedSubviews:@[
        ioTitle, controllerRow, metalRow, self.controllerInputLabel, self.metalProbeView
    ]];
    ioStack.translatesAutoresizingMaskIntoConstraints = NO;
    ioStack.axis = UILayoutConstraintAxisVertical;
    ioStack.spacing = 13.0;
    [ioCard addSubview:ioStack];

    [NSLayoutConstraint activateConstraints:@[
        [ioStack.leadingAnchor constraintEqualToAnchor:ioCard.leadingAnchor constant:16.0],
        [ioStack.trailingAnchor constraintEqualToAnchor:ioCard.trailingAnchor constant:-16.0],
        [ioStack.topAnchor constraintEqualToAnchor:ioCard.topAnchor constant:16.0],
        [ioStack.bottomAnchor constraintEqualToAnchor:ioCard.bottomAnchor constant:-16.0],
    ]];

    UIView *detailsCard = [self cardView];
    UILabel *detailsTitle = [[UILabel alloc] init];
    detailsTitle.translatesAutoresizingMaskIntoConstraints = NO;
    detailsTitle.text = @"Exécution";
    detailsTitle.textColor = UIColor.whiteColor;
    detailsTitle.font = [UIFont systemFontOfSize:18.0 weight:UIFontWeightBold];

    UILabel *stateLabel = [self mutedLabel:@"État"];
    self.stateValueLabel = [self valueLabel:@"Prêt"];
    UILabel *fileLabel = [self mutedLabel:@"Fichier"];
    self.fileValueLabel = [self valueLabel:@"Aucun"];

    UIStackView *stateRow = [self infoRowWithLeft:stateLabel right:self.stateValueLabel];
    UIStackView *fileRow = [self infoRowWithLeft:fileLabel right:self.fileValueLabel];

    UIStackView *detailStack = [[UIStackView alloc] initWithArrangedSubviews:@[
        detailsTitle, stateRow, fileRow
    ]];
    detailStack.translatesAutoresizingMaskIntoConstraints = NO;
    detailStack.axis = UILayoutConstraintAxisVertical;
    detailStack.spacing = 13.0;
    [detailsCard addSubview:detailStack];

    [NSLayoutConstraint activateConstraints:@[
        [detailStack.leadingAnchor constraintEqualToAnchor:detailsCard.leadingAnchor constant:16.0],
        [detailStack.trailingAnchor constraintEqualToAnchor:detailsCard.trailingAnchor constant:-16.0],
        [detailStack.topAnchor constraintEqualToAnchor:detailsCard.topAnchor constant:16.0],
        [detailStack.bottomAnchor constraintEqualToAnchor:detailsCard.bottomAnchor constant:-16.0],
    ]];

    UILabel *outputTitle = [[UILabel alloc] init];
    outputTitle.text = @"Sortie du programme";
    outputTitle.textColor = UIColor.whiteColor;
    outputTitle.font = [UIFont systemFontOfSize:18.0 weight:UIFontWeightBold];

    UIView *outputCard = [self cardView];
    self.guestOutputLabel = [[UILabel alloc] init];
    self.guestOutputLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.guestOutputLabel.numberOfLines = 0;
    self.guestOutputLabel.text = @"Aucune sortie pour le moment.";
    self.guestOutputLabel.font = [UIFont monospacedSystemFontOfSize:14.0 weight:UIFontWeightRegular];
    self.guestOutputLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.90];
    [outputCard addSubview:self.guestOutputLabel];
    [NSLayoutConstraint activateConstraints:@[
        [self.guestOutputLabel.leadingAnchor constraintEqualToAnchor:outputCard.leadingAnchor constant:16.0],
        [self.guestOutputLabel.trailingAnchor constraintEqualToAnchor:outputCard.trailingAnchor constant:-16.0],
        [self.guestOutputLabel.topAnchor constraintEqualToAnchor:outputCard.topAnchor constant:16.0],
        [self.guestOutputLabel.bottomAnchor constraintEqualToAnchor:outputCard.bottomAnchor constant:-16.0],
    ]];

    UILabel *diagnosticTitle = [[UILabel alloc] init];
    diagnosticTitle.text = @"Diagnostic live";
    diagnosticTitle.textColor = UIColor.whiteColor;
    diagnosticTitle.font = [UIFont systemFontOfSize:18.0 weight:UIFontWeightBold];

    UIView *diagnosticCard = [self cardView];
    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.statusLabel.numberOfLines = 0;
    self.statusLabel.text = self.lastDiagnostic;
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:13.0 weight:UIFontWeightRegular];
    self.statusLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.76];

    self.diagnosticCopyButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.diagnosticCopyButton.translatesAutoresizingMaskIntoConstraints = NO;
    [self.diagnosticCopyButton setTitle:@"Copier le diagnostic" forState:UIControlStateNormal];
    [self.diagnosticCopyButton setTitleColor:[UIColor colorWithRed:0.30 green:0.75 blue:1.0 alpha:1.0] forState:UIControlStateNormal];
    self.diagnosticCopyButton.titleLabel.font = [UIFont systemFontOfSize:14.0 weight:UIFontWeightSemibold];
    [self.diagnosticCopyButton addTarget:self action:@selector(copyDiagnostic) forControlEvents:UIControlEventTouchUpInside];

    [diagnosticCard addSubview:self.statusLabel];
    [diagnosticCard addSubview:self.diagnosticCopyButton];

    [NSLayoutConstraint activateConstraints:@[
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:diagnosticCard.leadingAnchor constant:16.0],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:diagnosticCard.trailingAnchor constant:-16.0],
        [self.statusLabel.topAnchor constraintEqualToAnchor:diagnosticCard.topAnchor constant:16.0],
        [self.diagnosticCopyButton.leadingAnchor constraintEqualToAnchor:diagnosticCard.leadingAnchor constant:16.0],
        [self.diagnosticCopyButton.topAnchor constraintEqualToAnchor:self.statusLabel.bottomAnchor constant:12.0],
        [self.diagnosticCopyButton.bottomAnchor constraintEqualToAnchor:diagnosticCard.bottomAnchor constant:-14.0],
    ]];

    UILabel *developerTitle = [[UILabel alloc] init];
    developerTitle.text = @"Développeur";
    developerTitle.textColor = [UIColor colorWithWhite:1.0 alpha:0.80];
    developerTitle.font = [UIFont systemFontOfSize:17.0 weight:UIFontWeightBold];

    UILabel *developerSubtitle = [[UILabel alloc] init];
    developerSubtitle.numberOfLines = 0;
    developerSubtitle.text = @"Noyau, FEX, Metal et diagnostics";
    developerSubtitle.textColor = [UIColor colorWithWhite:1.0 alpha:0.42];
    developerSubtitle.font = [UIFont systemFontOfSize:12.0 weight:UIFontWeightMedium];

    UIView *developerCard = [self cardView];
    UIStackView *developerStack = [[UIStackView alloc] initWithArrangedSubviews:@[
        developerSubtitle,
        self.testButton,
        self.multithreadTestButton,
        ioCard,
        detailsCard,
        outputTitle,
        outputCard,
        diagnosticTitle,
        diagnosticCard
    ]];
    developerStack.translatesAutoresizingMaskIntoConstraints = NO;
    developerStack.axis = UILayoutConstraintAxisVertical;
    developerStack.spacing = 12.0;
    [developerCard addSubview:developerStack];
    [NSLayoutConstraint activateConstraints:@[
        [developerStack.leadingAnchor constraintEqualToAnchor:developerCard.leadingAnchor constant:12.0],
        [developerStack.trailingAnchor constraintEqualToAnchor:developerCard.trailingAnchor constant:-12.0],
        [developerStack.topAnchor constraintEqualToAnchor:developerCard.topAnchor constant:12.0],
        [developerStack.bottomAnchor constraintEqualToAnchor:developerCard.bottomAnchor constant:-12.0],
    ]];

    UILabel *legal = [[UILabel alloc] init];
    legal.translatesAutoresizingMaskIntoConstraints = NO;
    legal.numberOfLines = 0;
    legal.font = [UIFont systemFontOfSize:12.0];
    legal.textColor = [UIColor colorWithWhite:1.0 alpha:0.42];
    legal.text = @"Aucun jeu, firmware, clé ou contenu PlayStation n’est inclus. Utilise uniquement des fichiers que tu es autorisé à utiliser. Les licences et notices tierces sont incluses dans l’app.";

    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
        brand,
        subtitle,
        backendCard,
        libraryTitle,
        self.libraryCategoryControl,
        libraryCard,
        self.importButton,
        developerTitle,
        developerCard,
        legal
    ]];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 16.0;

    [content addSubview:stack];

    [self refreshHardwareStatus];
    [self.metalProbeView setNeedsDisplay];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(controllerDidChange:)
                                                 name:GCControllerDidConnectNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(controllerDidChange:)
                                                 name:GCControllerDidDisconnectNotification
                                               object:nil];

    [NSLayoutConstraint activateConstraints:@[
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],

        [content.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor],
        [content.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor],
        [content.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
        [content.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor],
        [content.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor],

        [stack.leadingAnchor constraintEqualToAnchor:content.safeAreaLayoutGuide.leadingAnchor constant:20.0],
        [stack.trailingAnchor constraintEqualToAnchor:content.safeAreaLayoutGuide.trailingAnchor constant:-20.0],
        [stack.topAnchor constraintEqualToAnchor:content.safeAreaLayoutGuide.topAnchor constant:18.0],
        [stack.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-28.0],
    ]];
}

- (void)controllerDidChange:(NSNotification *)note {
    (void)note;
    [self refreshHardwareStatus];
}

- (void)publishControllerState:(GCController *)controller {
    GCExtendedGamepad *pad = controller.extendedGamepad;
    if (!pad) {
        maxps4_backend_set_controller_state(0, 0, 0, 0, 0, 0, 0);
        self.controllerInputLabel.text = @"Entrées manette : profil étendu indisponible";
        return;
    }

    unsigned int buttons = 0;
    if (pad.buttonA.pressed) buttons |= 1u << 0;
    if (pad.buttonB.pressed) buttons |= 1u << 1;
    if (pad.buttonX.pressed) buttons |= 1u << 2;
    if (pad.buttonY.pressed) buttons |= 1u << 3;
    if (pad.leftShoulder.pressed) buttons |= 1u << 4;
    if (pad.rightShoulder.pressed) buttons |= 1u << 5;
    if (pad.dpad.up.pressed) buttons |= 1u << 6;
    if (pad.dpad.down.pressed) buttons |= 1u << 7;
    if (pad.dpad.left.pressed) buttons |= 1u << 8;
    if (pad.dpad.right.pressed) buttons |= 1u << 9;
    if (pad.buttonMenu.pressed) buttons |= 1u << 10;

    const float lx = pad.leftThumbstick.xAxis.value;
    const float ly = pad.leftThumbstick.yAxis.value;
    const float rx = pad.rightThumbstick.xAxis.value;
    const float ry = pad.rightThumbstick.yAxis.value;
    const float l2 = pad.leftTrigger.value;
    const float r2 = pad.rightTrigger.value;

    maxps4_backend_set_controller_state(buttons, lx, ly, rx, ry, l2, r2);

    // Visible Metal probe: left stick moves the square; triggers change size.
    // Face button A toggles the background pulse for an obvious button test.
    self.probeX = lx * 0.78f;
    self.probeY = ly * 0.70f;
    self.probeScale = 0.65f + (r2 * 0.95f) - (l2 * 0.35f);
    if (self.probeScale < 0.30f) self.probeScale = 0.30f;
    if (self.probeScale > 1.60f) self.probeScale = 1.60f;
    self.probeButtons = buttons;
    self.metalProbeView.clearColor = (buttons & 1u)
        ? MTLClearColorMake(0.18, 0.04, 0.32, 1.0)
        : MTLClearColorMake(0.02, 0.24, 0.72, 1.0);
    [self.metalProbeView setNeedsDisplay];

    self.controllerInputLabel.text =
        [NSString stringWithFormat:@"Guest input • btn=0x%03X • L %.2f/%.2f • R %.2f/%.2f • LT %.2f RT %.2f",
                                   buttons, lx, ly, rx, ry, l2, r2];
}

- (void)refreshHardwareStatus {
    id<MTLDevice> device = self.metalProbeView.device ?: MTLCreateSystemDefaultDevice();
    self.metalValueLabel.text = device ? [NSString stringWithFormat:@"Disponible • %@", device.name ?: @"GPU Apple"] : @"Indisponible";

    NSArray<GCController *> *controllers = GCController.controllers;
    if (controllers.count == 0) {
        self.controllerValueLabel.text = @"Aucune";
        self.controllerInputLabel.text = @"Entrées manette : en attente";
        maxps4_backend_set_controller_state(0, 0, 0, 0, 0, 0, 0);
        return;
    }

    GCController *controller = controllers.firstObject;
    NSString *name = controller.vendorName.length ? controller.vendorName : @"Manette connectée";
    self.controllerValueLabel.text = name;

    __unsafe_unretained MaxPS4IntegratedViewController *unsafeSelf = self;
    controller.extendedGamepad.valueChangedHandler = ^(GCExtendedGamepad *gamepad, GCControllerElement *element) {
        (void)gamepad;
        (void)element;
        dispatch_async(dispatch_get_main_queue(), ^{
            [unsafeSelf publishControllerState:controller];
        });
    };
    [self publishControllerState:controller];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
}

- (void)drawInMTKView:(MTKView *)view {
    MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!pass || !drawable || !self.metalCommandQueue) return;

    // Synchronize the bounded guest RGBA8 texture table. Each texture_id is
    // cached independently so one scene can reference several guest resources.
    for (unsigned int slot = 0; slot < MAXPS4_GUEST_TEXTURE_MAX_RESOURCES; ++slot) {
        MaxPS4GuestTexture guestTexture{};
        if (!maxps4_backend_guest_texture_at(slot, &guestTexture) ||
            guestTexture.texture_id == 0 ||
            guestTexture.width == 0 || guestTexture.height == 0 ||
            guestTexture.byte_count != guestTexture.width * guestTexture.height * 4u) {
            continue;
        }

        NSNumber *textureKey = @(guestTexture.texture_id);
        NSNumber *knownSequence = self.guestUploadedTextureSequences[textureKey];
        if (knownSequence &&
            knownSequence.unsignedIntValue == guestTexture.sequence) {
            continue;
        }

        MTLTextureDescriptor *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:guestTexture.width
                                                              height:guestTexture.height
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [view.device newTextureWithDescriptor:desc];
        if (texture) {
            [texture replaceRegion:MTLRegionMake2D(0, 0, guestTexture.width, guestTexture.height)
                       mipmapLevel:0
                         withBytes:guestTexture.rgba
                       bytesPerRow:guestTexture.width * 4u];
            self.guestUploadedTextures[textureKey] = texture;
            self.guestUploadedTextureSequences[textureKey] = @(guestTexture.sequence);
        }
    }

    MaxPS4GuestSceneFrame scene{};
    const BOOL hasScene = maxps4_backend_guest_scene_frame(&scene);

    MaxPS4GuestFrame frame{};
    const BOOL hasGuestFrame = !hasScene && maxps4_backend_guest_frame(&frame);

    if (hasScene) {
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake(scene.clear_red, scene.clear_green,
                              scene.clear_blue, scene.clear_alpha);
    } else if (hasGuestFrame) {
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake(frame.clear_red, frame.clear_green,
                              frame.clear_blue, frame.clear_alpha);
    }

    id<MTLCommandBuffer> commandBuffer = [self.metalCommandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];

    if (self.metalProbePipeline) {
        [encoder setRenderPipelineState:self.metalProbePipeline];

        if (hasScene) {
            const unsigned int count =
                MIN(scene.primitive_count, (unsigned int)MAXPS4_GUEST_SCENE_MAX_PRIMITIVES);
            for (unsigned int i = 0; i < count; ++i) {
                const MaxPS4GuestPrimitive primitive = scene.primitives[i];
                const float cs = (float)std::cos((double)primitive.rotation);
                const float sn = (float)std::sin((double)primitive.rotation);
                const float hx = primitive.width * 0.5f;
                const float hy = primitive.height * 0.5f;

                id<MTLTexture> sceneTexture = nil;
                if (primitive.type == MAXPS4_GUEST_PRIMITIVE_TEXTURED_QUAD) {
                    sceneTexture = self.metalProbeTexture;
                } else if (primitive.type == MAXPS4_GUEST_PRIMITIVE_GUEST_TEXTURED_QUAD &&
                           primitive.texture_id != 0) {
                    sceneTexture = self.guestUploadedTextures[@(primitive.texture_id)];
                }

                if (sceneTexture && self.metalTexturePipeline) {
                    const float local[8] = {
                        -hx, -hy,
                         hx, -hy,
                        -hx,  hy,
                         hx,  hy,
                    };
                    const float uv[8] = {
                        0.0f, 1.0f,
                        1.0f, 1.0f,
                        0.0f, 0.0f,
                        1.0f, 0.0f,
                    };
                    float vertices[16];
                    for (int v = 0; v < 4; ++v) {
                        const float lx = local[v * 2];
                        const float ly = local[v * 2 + 1];
                        vertices[v * 4] =
                            primitive.x + lx * cs - ly * sn;
                        vertices[v * 4 + 1] =
                            primitive.y + lx * sn + ly * cs;
                        vertices[v * 4 + 2] = uv[v * 2];
                        vertices[v * 4 + 3] = uv[v * 2 + 1];
                    }
                    [encoder setRenderPipelineState:self.metalTexturePipeline];
                    [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
                    [encoder setFragmentTexture:sceneTexture atIndex:0];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
                    continue;
                }

                [encoder setRenderPipelineState:self.metalProbePipeline];
                const float color[4] = {
                    primitive.red, primitive.green, primitive.blue, primitive.alpha
                };
                [encoder setFragmentBytes:color length:sizeof(color) atIndex:0];

                if (primitive.type == MAXPS4_GUEST_PRIMITIVE_TRIANGLE) {
                    const float local[6] = {
                         0.0f,  hy,
                        -hx,   -hy,
                         hx,   -hy,
                    };
                    float vertices[6];
                    for (int v = 0; v < 3; ++v) {
                        const float lx = local[v * 2];
                        const float ly = local[v * 2 + 1];
                        vertices[v * 2] =
                            primitive.x + lx * cs - ly * sn;
                        vertices[v * 2 + 1] =
                            primitive.y + lx * sn + ly * cs;
                    }
                    [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                } else {
                    const float local[8] = {
                        -hx, -hy,
                         hx, -hy,
                        -hx,  hy,
                         hx,  hy,
                    };
                    float vertices[8];
                    for (int v = 0; v < 4; ++v) {
                        const float lx = local[v * 2];
                        const float ly = local[v * 2 + 1];
                        vertices[v * 2] =
                            primitive.x + lx * cs - ly * sn;
                        vertices[v * 2 + 1] =
                            primitive.y + lx * sn + ly * cs;
                    }
                    [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
                }
            }
        } else if (hasGuestFrame) {
            const unsigned int count =
                MIN(frame.rect_count, (unsigned int)MAXPS4_GUEST_FRAME_MAX_RECTS);
            for (unsigned int i = 0; i < count; ++i) {
                const MaxPS4GuestRect rect = frame.rects[i];
                const float halfW = 0.15f * rect.scale;
                const float halfH = 0.28f * rect.scale;
                const float vertices[8] = {
                    rect.x - halfW, rect.y - halfH,
                    rect.x + halfW, rect.y - halfH,
                    rect.x - halfW, rect.y + halfH,
                    rect.x + halfW, rect.y + halfH,
                };
                const float color[4] = {
                    rect.red, rect.green, rect.blue, rect.alpha
                };
                [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
                [encoder setFragmentBytes:color length:sizeof(color) atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            }
        } else {
            float cx = self.probeX;
            float cy = self.probeY;
            float scale = self.probeScale;
            float color[4] = {0.15f, 0.95f, 1.0f, 1.0f};

            MaxPS4GuestRenderState guest{};
            if (maxps4_backend_guest_render_state(&guest)) {
                cx = guest.x;
                cy = guest.y;
                scale = guest.scale;
                color[0] = guest.red;
                color[1] = guest.green;
                color[2] = guest.blue;
                color[3] = guest.alpha;
            }

            const float halfW = 0.15f * scale;
            const float halfH = 0.28f * scale;
            const float vertices[8] = {
                cx - halfW, cy - halfH,
                cx + halfW, cy - halfH,
                cx - halfW, cy + halfH,
                cx + halfW, cy + halfH,
            };
            [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
            [encoder setFragmentBytes:color length:sizeof(color) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        }
    }

    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

- (NSAttributedString *)brandText {
    NSMutableAttributedString *text = [[NSMutableAttributedString alloc] initWithString:@"MaxPS4"];
    [text addAttribute:NSForegroundColorAttributeName
                 value:UIColor.whiteColor
                 range:NSMakeRange(0, 3)];
    [text addAttribute:NSForegroundColorAttributeName
                 value:[UIColor colorWithRed:0.10 green:0.64 blue:1.0 alpha:1.0]
                 range:NSMakeRange(3, 3)];
    return text;
}

- (UIView *)cardView {
    UIView *view = [[UIView alloc] init];
    view.translatesAutoresizingMaskIntoConstraints = NO;
    view.backgroundColor = [UIColor colorWithRed:0.055 green:0.065 blue:0.085 alpha:0.96];
    view.layer.cornerRadius = 20.0;
    view.layer.borderWidth = 1.0;
    view.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.075].CGColor;
    return view;
}

- (UIButton *)actionButtonWithTitle:(NSString *)title
                            subtitle:(NSString *)subtitle
                             primary:(BOOL)primary {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    button.titleLabel.numberOfLines = 0;
    button.titleLabel.font = [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold];

    NSMutableAttributedString *text = [[NSMutableAttributedString alloc] initWithString:title
                                                                              attributes:@{
        NSForegroundColorAttributeName: UIColor.whiteColor,
        NSFontAttributeName: [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold]
    }];
    [text appendAttributedString:[[NSAttributedString alloc] initWithString:[NSString stringWithFormat:@"\n%@", subtitle]
                                                                 attributes:@{
        NSForegroundColorAttributeName: [UIColor colorWithWhite:1.0 alpha:0.63],
        NSFontAttributeName: [UIFont systemFontOfSize:13.0 weight:UIFontWeightRegular]
    }]];
    [button setAttributedTitle:text forState:UIControlStateNormal];

    button.contentEdgeInsets = UIEdgeInsetsMake(16.0, 18.0, 16.0, 18.0);
    button.layer.cornerRadius = 22.0;
    button.layer.borderWidth = 1.0;

    if (primary) {
        button.backgroundColor = [UIColor colorWithRed:0.12 green:0.36 blue:0.98 alpha:1.0];
        button.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.12].CGColor;
        button.layer.shadowColor = [UIColor blackColor].CGColor;
        button.layer.shadowOpacity = 0.22;
        button.layer.shadowRadius = 10.0;
        button.layer.shadowOffset = CGSizeMake(0, 5);
    } else {
        button.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.045];
        button.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.07].CGColor;
    }
    return button;
}

- (UILabel *)mutedLabel:(NSString *)text {
    UILabel *label = [[UILabel alloc] init];
    label.text = text;
    label.textColor = [UIColor colorWithWhite:1.0 alpha:0.50];
    label.font = [UIFont systemFontOfSize:13.0];
    return label;
}

- (UILabel *)valueLabel:(NSString *)text {
    UILabel *label = [[UILabel alloc] init];
    label.text = text;
    label.textColor = UIColor.whiteColor;
    label.textAlignment = NSTextAlignmentRight;
    label.font = [UIFont systemFontOfSize:13.0 weight:UIFontWeightSemibold];
    label.numberOfLines = 2;
    return label;
}

- (UIStackView *)infoRowWithLeft:(UIView *)left right:(UIView *)right {
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[left, right]];
    row.axis = UILayoutConstraintAxisHorizontal;
    row.alignment = UIStackViewAlignmentCenter;
    row.spacing = 12.0;
    return row;
}

- (void)setDiagnostic:(NSString *)text {
    self.lastDiagnostic = text ?: @"aucun diagnostic";
    self.statusLabel.text = self.lastDiagnostic;
}

- (void)copyDiagnostic {
    // Always copy what is currently visible. During live polling this avoids
    // copying a stale diagnostic between timer updates.
    NSString *text = self.statusLabel.text;
    if (text.length == 0) text = self.lastDiagnostic;
    if (text.length == 0) text = @"aucun diagnostic";

    UIPasteboard *pasteboard = UIPasteboard.generalPasteboard;
    [pasteboard setItems:@[@{ UIPasteboardTypeListString.firstObject : text }]
                 options:@{}];

    self.copyFeedbackGeneration += 1;
    const NSUInteger feedbackGeneration = self.copyFeedbackGeneration;

    self.diagnosticCopyButton.enabled = NO;
    [self.diagnosticCopyButton setTitle:@"Diagnostic copié ✓" forState:UIControlStateNormal];

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.9 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        if (feedbackGeneration != self.copyFeedbackGeneration) return;
        [self.diagnosticCopyButton setTitle:@"Copier le diagnostic" forState:UIControlStateNormal];
        self.diagnosticCopyButton.enabled = YES;
    });
}


- (NSString *)libraryRootPath {
    NSArray<NSString *> *paths =
        NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,
                                             NSUserDomainMask, YES);
    NSString *base = paths.firstObject ?: NSTemporaryDirectory();
    NSString *root = [base stringByAppendingPathComponent:@"MaxPS4Library"];
    [[NSFileManager defaultManager] createDirectoryAtPath:root
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    return root;
}

- (void)loadLibrary {
    NSArray *saved =
        [[NSUserDefaults standardUserDefaults]
            arrayForKey:@"MaxPS4.LibraryGames.Integrated.v1"];

    self.libraryGames = [NSMutableArray array];
    for (NSDictionary *entry in saved ?: @[]) {
        NSMutableDictionary *game = [entry mutableCopy];
        NSString *relative = game[@"relativePath"];
        if (relative.length == 0) continue;

        NSString *path = [[self libraryRootPath]
            stringByAppendingPathComponent:relative];
        if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
            [self.libraryGames addObject:game];
        }
    }
    [self saveLibrary];
}

- (void)saveLibrary {
    [[NSUserDefaults standardUserDefaults]
        setObject:self.libraryGames ?: @[]
           forKey:@"MaxPS4.LibraryGames.Integrated.v1"];
}

- (NSArray<NSMutableDictionary *> *)filteredLibraryGames {
    NSInteger category = self.libraryCategoryControl
        ? self.libraryCategoryControl.selectedSegmentIndex
        : 0;

    NSPredicate *predicate = [NSPredicate predicateWithBlock:
        ^BOOL(NSMutableDictionary *game, NSDictionary *bindings) {
            (void)bindings;
            if (category == 1) return [game[@"favorite"] boolValue];
            if (category == 2) return [game[@"lastPlayedAt"] doubleValue] > 0.0;
            if (category == 3) return [game[@"homebrew"] boolValue];
            return YES;
        }];

    NSArray<NSMutableDictionary *> *filtered =
        [self.libraryGames filteredArrayUsingPredicate:predicate];

    return [filtered sortedArrayUsingComparator:
        ^NSComparisonResult(NSMutableDictionary *a, NSMutableDictionary *b) {
            double av = category == 2
                ? [a[@"lastPlayedAt"] doubleValue]
                : [a[@"importedAt"] doubleValue];
            double bv = category == 2
                ? [b[@"lastPlayedAt"] doubleValue]
                : [b[@"importedAt"] doubleValue];
            if (av > bv) return NSOrderedAscending;
            if (av < bv) return NSOrderedDescending;
            return NSOrderedSame;
        }];
}

- (UIButton *)smallLibraryButtonWithTitle:(NSString *)title
                                   action:(SEL)action
                                      tag:(NSInteger)tag {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    button.titleLabel.font =
        [UIFont systemFontOfSize:13.0 weight:UIFontWeightSemibold];
    button.contentEdgeInsets = UIEdgeInsetsMake(8.0, 11.0, 8.0, 11.0);
    button.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.07];
    button.layer.cornerRadius = 11.0;
    button.tag = tag;
    [button addTarget:self action:action
     forControlEvents:UIControlEventTouchUpInside];
    return button;
}

- (void)refreshLibraryUI {
    if (!self.libraryStack) return;

    for (UIView *view in self.libraryStack.arrangedSubviews.copy) {
        [self.libraryStack removeArrangedSubview:view];
        [view removeFromSuperview];
    }

    self.visibleLibraryGames = [self filteredLibraryGames];

    if (self.visibleLibraryGames.count == 0) {
        NSInteger category = self.libraryCategoryControl.selectedSegmentIndex;
        NSArray<NSString *> *messages = @[
            @"Aucun jeu importé. Utilise le bouton d’import ci-dessus.",
            @"Aucun favori. Appuie sur ♡ sur un jeu pour l’ajouter.",
            @"Aucun jeu récent. Les jeux lancés apparaîtront ici.",
            @"Aucun homebrew. Appuie sur HB sur un jeu pour le classer ici."
        ];
        NSInteger safeIndex = MAX(0, MIN(category, (NSInteger)messages.count - 1));
        self.libraryEmptyLabel.text = messages[(NSUInteger)safeIndex];
        [self.libraryStack addArrangedSubview:self.libraryEmptyLabel];
        return;
    }

    [self.visibleLibraryGames enumerateObjectsUsingBlock:
        ^(NSMutableDictionary *game, NSUInteger idx, BOOL *stop) {
            (void)stop;

            UIView *rowCard = [[UIView alloc] init];
            rowCard.translatesAutoresizingMaskIntoConstraints = NO;
            rowCard.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.045];
            rowCard.layer.cornerRadius = 16.0;
            rowCard.layer.borderWidth = 1.0;
            rowCard.layer.borderColor =
                [UIColor colorWithWhite:1.0 alpha:0.08].CGColor;

            UIView *cover = [[UIView alloc] init];
            cover.translatesAutoresizingMaskIntoConstraints = NO;
            cover.backgroundColor = [UIColor colorWithRed:0.11 green:0.24 blue:0.62 alpha:1.0];
            cover.layer.cornerRadius = 14.0;

            UILabel *coverMark = [[UILabel alloc] init];
            coverMark.translatesAutoresizingMaskIntoConstraints = NO;
            coverMark.text = @"PS4";
            coverMark.textColor = UIColor.whiteColor;
            coverMark.font = [UIFont systemFontOfSize:16.0 weight:UIFontWeightBlack];
            coverMark.textAlignment = NSTextAlignmentCenter;
            [cover addSubview:coverMark];

            UILabel *name = [[UILabel alloc] init];
            name.numberOfLines = 2;
            name.textColor = UIColor.whiteColor;
            name.font = [UIFont systemFontOfSize:17.0 weight:UIFontWeightBold];

            UILabel *meta = [[UILabel alloc] init];
            meta.textColor = [UIColor colorWithWhite:1.0 alpha:0.46];
            meta.font = [UIFont systemFontOfSize:12.0 weight:UIFontWeightMedium];

            NSMutableArray<NSString *> *badges = [NSMutableArray array];
            if ([game[@"favorite"] boolValue]) [badges addObject:@"Favori"];
            if ([game[@"homebrew"] boolValue]) [badges addObject:@"Homebrew"];
            name.text = game[@"name"] ?: @"Jeu PS4";
            meta.text = badges.count ? [badges componentsJoinedByString:@"  •  "] : @"Prêt à lancer";

            UIButton *play =
                [self smallLibraryButtonWithTitle:@"▶  Lancer"
                                           action:@selector(launchLibraryGame:)
                                              tag:(NSInteger)idx];
            play.backgroundColor = [UIColor colorWithRed:0.12 green:0.36 blue:0.98 alpha:1.0];

            NSString *heart = [game[@"favorite"] boolValue] ? @"♥" : @"♡";
            UIButton *favorite =
                [self smallLibraryButtonWithTitle:heart
                                           action:@selector(toggleLibraryFavorite:)
                                              tag:(NSInteger)idx];

            NSString *hb = [game[@"homebrew"] boolValue] ? @"HB ✓" : @"HB";
            UIButton *homebrew =
                [self smallLibraryButtonWithTitle:hb
                                           action:@selector(toggleLibraryHomebrew:)
                                              tag:(NSInteger)idx];

            UIStackView *actions =
                [[UIStackView alloc] initWithArrangedSubviews:@[play, favorite, homebrew]];
            actions.axis = UILayoutConstraintAxisHorizontal;
            actions.spacing = 7.0;
            actions.distribution = UIStackViewDistributionFillProportionally;

            UIStackView *gameText =
                [[UIStackView alloc] initWithArrangedSubviews:@[name, meta, actions]];
            gameText.axis = UILayoutConstraintAxisVertical;
            gameText.spacing = 7.0;

            UIStackView *content =
                [[UIStackView alloc] initWithArrangedSubviews:@[cover, gameText]];
            content.translatesAutoresizingMaskIntoConstraints = NO;
            content.axis = UILayoutConstraintAxisHorizontal;
            content.alignment = UIStackViewAlignmentCenter;
            content.spacing = 13.0;

            [rowCard addSubview:content];
            [NSLayoutConstraint activateConstraints:@[
                [cover.widthAnchor constraintEqualToConstant:72.0],
                [cover.heightAnchor constraintEqualToConstant:92.0],
                [coverMark.centerXAnchor constraintEqualToAnchor:cover.centerXAnchor],
                [coverMark.centerYAnchor constraintEqualToAnchor:cover.centerYAnchor],
                [content.leadingAnchor constraintEqualToAnchor:rowCard.leadingAnchor constant:12.0],
                [content.trailingAnchor constraintEqualToAnchor:rowCard.trailingAnchor constant:-12.0],
                [content.topAnchor constraintEqualToAnchor:rowCard.topAnchor constant:12.0],
                [content.bottomAnchor constraintEqualToAnchor:rowCard.bottomAnchor constant:-12.0],
            ]];

            [self.libraryStack addArrangedSubview:rowCard];
        }];
}

- (void)libraryCategoryChanged:(UISegmentedControl *)sender {
    (void)sender;
    [self refreshLibraryUI];
}

- (NSMutableDictionary *)visibleLibraryGameForButton:(UIButton *)button {
    NSInteger index = button.tag;
    if (index < 0 || index >= (NSInteger)self.visibleLibraryGames.count) {
        return nil;
    }
    return self.visibleLibraryGames[(NSUInteger)index];
}

- (void)toggleLibraryFavorite:(UIButton *)sender {
    NSMutableDictionary *game = [self visibleLibraryGameForButton:sender];
    if (!game) return;
    game[@"favorite"] = @(![game[@"favorite"] boolValue]);
    [self saveLibrary];
    [self refreshLibraryUI];
}

- (void)toggleLibraryHomebrew:(UIButton *)sender {
    NSMutableDictionary *game = [self visibleLibraryGameForButton:sender];
    if (!game) return;
    game[@"homebrew"] = @(![game[@"homebrew"] boolValue]);
    [self saveLibrary];
    [self refreshLibraryUI];
}

- (NSURL *)persistImportedExecutable:(NSURL *)url {
    if (!url) return nil;

    NSString *uuid = NSUUID.UUID.UUIDString;
    NSString *directory =
        [[self libraryRootPath] stringByAppendingPathComponent:uuid];

    NSError *error = nil;
    if (![[NSFileManager defaultManager]
            createDirectoryAtPath:directory
      withIntermediateDirectories:YES
                       attributes:nil
                            error:&error]) {
        [self setDiagnostic:[NSString stringWithFormat:
            @"Import bibliothèque impossible : %@",
            error.localizedDescription ?: @"erreur inconnue"]];
        return nil;
    }

    NSString *destination =
        [directory stringByAppendingPathComponent:url.lastPathComponent];

    if (![[NSFileManager defaultManager] copyItemAtPath:url.path
                                                 toPath:destination
                                                  error:&error]) {
        [[NSFileManager defaultManager] removeItemAtPath:directory error:nil];
        [self setDiagnostic:[NSString stringWithFormat:
            @"Copie dans la bibliothèque impossible : %@",
            error.localizedDescription ?: @"erreur inconnue"]];
        return nil;
    }

    NSString *displayName = url.lastPathComponent.stringByDeletingPathExtension;
    if ([url.lastPathComponent.lowercaseString isEqualToString:@"eboot.bin"]) {
        NSString *parent = url.URLByDeletingLastPathComponent.lastPathComponent;
        if (parent.length) displayName = parent;
    }
    if (displayName.length == 0) displayName = url.lastPathComponent;

    NSMutableDictionary *game = [@{
        @"id": uuid,
        @"name": displayName,
        @"relativePath":
            [uuid stringByAppendingPathComponent:url.lastPathComponent],
        @"importedAt": @([NSDate date].timeIntervalSince1970),
        @"lastPlayedAt": @0.0,
        @"favorite": @NO,
        @"homebrew": @NO
    } mutableCopy];

    [self.libraryGames insertObject:game atIndex:0];
    [self saveLibrary];
    [self refreshLibraryUI];

    return [NSURL fileURLWithPath:destination];
}

- (void)launchLibraryGame:(UIButton *)sender {
    NSMutableDictionary *game = [self visibleLibraryGameForButton:sender];
    if (!game) return;

    NSString *relative = game[@"relativePath"];
    NSString *path =
        [[self libraryRootPath] stringByAppendingPathComponent:relative ?: @""];

    NSDictionary *attrs =
        [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    unsigned long long size = [attrs[NSFileSize] unsignedLongLongValue];

    game[@"lastPlayedAt"] = @([NSDate date].timeIntervalSince1970);
    [self saveLibrary];
    [self refreshLibraryUI];

    [self launchManagedExecutableAtPath:path
                           displayName:(game[@"name"] ?: @"Jeu PS4")
                              fileSize:size];
}

- (void)launchManagedExecutableAtPath:(NSString *)path
                          displayName:(NSString *)displayName
                            fileSize:(unsigned long long)fileSize {
    if (self.bootInProgress) {
        [self setDiagnostic:
            @"Un guest FEX est déjà en cours. Attends sa fin avant d’en lancer un autre."];
        return;
    }

    if (path.length == 0 ||
        ![[NSFileManager defaultManager] fileExistsAtPath:path]) {
        [self setDiagnostic:@"Le fichier du jeu n’existe plus dans la bibliothèque."];
        [self loadLibrary];
        [self refreshLibraryUI];
        return;
    }

    if (!maxps4_backend_validate_executable(path.fileSystemRepresentation)) {
        const char *detail = maxps4_backend_diagnostic();
        [self setDiagnostic:[NSString stringWithFormat:
            @"Fichier bibliothèque refusé : %@",
            detail ? [NSString stringWithUTF8String:detail] : @"invalide"]];
        return;
    }

    if (!maxps4_core_jit_available()) {
        const char *jitDetail = maxps4_core_jit_diagnostic();
        NSString *jitText = jitDetail
            ? [NSString stringWithUTF8String:jitDetail]
            : @"aucun diagnostic JIT";
        self.stateValueLabel.text = @"JIT indisponible";
        [self setDiagnostic:[NSString stringWithFormat:
            @"%@ est prêt, mais le JIT est requis : %@",
            displayName, jitText]];
        return;
    }

    self.bootInProgress = YES;
    self.bootGeneration += 1;
    const NSUInteger generation = self.bootGeneration;
    self.bootStartedAt = [NSDate date];
    self.importButton.enabled = NO;
    self.testButton.enabled = NO;
    self.multithreadTestButton.enabled = NO;
    self.stateValueLabel.text = @"Validation OK";
    self.fileValueLabel.text =
        [NSString stringWithFormat:@"%@ • %llu o", displayName, fileSize];
    self.guestOutputLabel.text = @"En attente de sortie…";

    [self setDiagnostic:[NSString stringWithFormat:
        @"Bibliothèque → loader → FEX : %@\nTaille : %llu octets",
        displayName, fileSize]];

    [self startDiagnosticPollingForGeneration:generation];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL booted = maxps4_backend_boot(path.fileSystemRepresentation);
        const char *bootDetail = maxps4_backend_diagnostic();
        NSString *bootText = bootDetail
            ? [NSString stringWithUTF8String:bootDetail]
            : @"aucun détail";

        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != self.bootGeneration) return;

            [self stopDiagnosticPolling];
            self.bootInProgress = NO;
            self.importButton.enabled = YES;
            self.testButton.enabled = YES;
            self.multithreadTestButton.enabled = YES;

            NSTimeInterval elapsed =
                self.bootStartedAt ? -[self.bootStartedAt timeIntervalSinceNow] : 0;

            char finalOutputBuf[4096] = {};
            maxps4_backend_live_output(finalOutputBuf, sizeof(finalOutputBuf));
            NSString *finalOutput =
                [NSString stringWithUTF8String:finalOutputBuf] ?: @"";
            self.guestOutputLabel.text =
                finalOutput.length ? finalOutput : @"Aucune sortie produite.";

            self.stateValueLabel.text = booted ? @"Terminé" : @"Handoff arrêté";
            [self setDiagnostic:[NSString stringWithFormat:
                @"%@\nDurée : %.2f s\n\n%@",
                booted ? @"Exécution terminée" : @"Exécution arrêtée",
                elapsed,
                bootText]];
        });
    });
}

- (void)runMultithreadKernelTest {
    if (self.bootInProgress) {
        [self setDiagnostic:@"Un guest FEX est déjà en cours. Attends sa fin avant de lancer le test multi-thread."];
        return;
    }

    NSString *path =
        [NSBundle.mainBundle pathForResource:@"MaxPS4KernelThreadTest"
                                     ofType:@"elf"];
    if (path.length == 0) {
        self.stateValueLabel.text = @"Test absent";
        [self setDiagnostic:
            @"Le test MaxPS4Kernel multi-thread n’est pas présent dans ce build."];
        return;
    }

    NSDictionary *attrs =
        [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil];
    unsigned long long fileSize =
        [attrs[NSFileSize] unsignedLongLongValue];

    self.stateValueLabel.text = @"Test multi-thread…";
    [self setDiagnostic:
        @"Test intégré MaxPS4Kernel : thr_new(455) → second contexte FEX → thr_self(432) → thr_exit(431). JIT/StikDebug doit être actif."];

    [self launchManagedExecutableAtPath:path
                           displayName:@"MaxPS4Kernel multi-thread"
                              fileSize:fileSize];
}

- (void)runBackendTest {
    if (self.bootInProgress) {
        [self setDiagnostic:@"Un guest FEX est en cours. Attends sa fin avant de lancer le self-test."];
        return;
    }

    self.testButton.enabled = NO;
    self.stateValueLabel.text = @"Test…";
    [self setDiagnostic:@"Test du backend shadPS4/FEX en cours…"];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL ok = maxps4_backend_self_test();
        const char *detail = maxps4_backend_diagnostic();
        NSString *text = detail ? [NSString stringWithUTF8String:detail] : @"aucun détail";

        dispatch_async(dispatch_get_main_queue(), ^{
            self.stateValueLabel.text = ok ? @"Backend OK" : @"Échec backend";
            [self setDiagnostic:(ok
                ? [NSString stringWithFormat:@"Backend shadPS4/FEX : OK\n%@", text]
                : [NSString stringWithFormat:@"Backend shadPS4/FEX : échec\n%@", text])];
            self.testButton.enabled = YES;
        });
    });
}

- (void)pickExecutable {
    if (self.bootInProgress) {
        [self setDiagnostic:@"Un guest FEX est déjà en cours. Attends la fin de l’exécution avant d’en lancer un autre."];
        return;
    }

    UIDocumentPickerViewController *picker =
        [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.data"]
                                                               inMode:UIDocumentPickerModeOpen];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)startDiagnosticPollingForGeneration:(NSUInteger)generation {
    [self.diagnosticTimer invalidate];
    self.diagnosticTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                           repeats:YES
                                                             block:^(NSTimer *timer) {
        if (!self.bootInProgress || generation != self.bootGeneration) {
            [timer invalidate];
            return;
        }

        char liveBuf[4096] = {};
        char outputBuf[4096] = {};
        maxps4_backend_live_diagnostic(liveBuf, sizeof(liveBuf));
        maxps4_backend_live_output(outputBuf, sizeof(outputBuf));
        NSString *live = [NSString stringWithUTF8String:liveBuf] ?: @"aucun diagnostic live";
        NSString *guestOutput = [NSString stringWithUTF8String:outputBuf] ?: @"";
        self.guestOutputLabel.text = guestOutput.length ? guestOutput : @"En attente de sortie…";
        NSTimeInterval elapsed = self.bootStartedAt ? -[self.bootStartedAt timeIntervalSinceNow] : 0;

        self.stateValueLabel.text = [NSString stringWithFormat:@"FEX • %.0f s", elapsed];
        [self setDiagnostic:[NSString stringWithFormat:
            @"%@\n\nDiagnostic live (%.0f s) :\n%@",
            self.fileValueLabel.text ?: @"Exécutable validé",
            elapsed,
            live]];
    }];
}

- (void)stopDiagnosticPolling {
    [self.diagnosticTimer invalidate];
    self.diagnosticTimer = nil;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
  didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url = urls.firstObject;
    if (!url) return;

    BOOL scoped = [url startAccessingSecurityScopedResource];

    NSError *attrError = nil;
    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:url.path
                                                                            error:&attrError];
    unsigned long long fileSize = [attrs[NSFileSize] unsignedLongLongValue];

    BOOL valid = maxps4_backend_validate_executable(url.fileSystemRepresentation);
    MaxPS4ImportKind importKind = valid
        ? maxps4_backend_import_kind(url.fileSystemRepresentation)
        : MAXPS4_IMPORT_INVALID;

    const char *detail = maxps4_backend_diagnostic();
    NSString *detailText = detail ? [NSString stringWithUTF8String:detail] : @"aucun détail";

    self.selectedURL = url;
    self.selectedFileSize = fileSize;
    self.fileValueLabel.text =
        [NSString stringWithFormat:@"%@ • %llu o", url.lastPathComponent, fileSize];

    if (!valid || importKind == MAXPS4_IMPORT_INVALID) {
        self.stateValueLabel.text = @"Refusé";
        [self setDiagnostic:[NSString stringWithFormat:
            @"Fichier refusé : %@\nTaille : %llu octets\n%@",
            url.lastPathComponent,
            fileSize,
            detailText]];
        if (scoped) [url stopAccessingSecurityScopedResource];
        return;
    }

    NSString *launchPath = url.path;
    NSString *displayFile = url.lastPathComponent;
    BOOL scopedForBoot = scoped;

    if (importKind == MAXPS4_IMPORT_EXECUTABLE) {
        NSURL *managedURL = [self persistImportedExecutable:url];
        if (managedURL) {
            launchPath = managedURL.path;
            if (scoped) {
                [url stopAccessingSecurityScopedResource];
                scopedForBoot = NO;
            }
        }
    }

    if (importKind == MAXPS4_IMPORT_PKG) {
        self.stateValueLabel.text = @"PKG reconnu";
        [self setDiagnostic:[NSString stringWithFormat:
            @"PKG sélectionné : %@\nTaille : %llu octets\n%@\n\nRecherche d’un ELF/SELF homebrew stocké directement et lisible…",
            url.lastPathComponent,
            fileSize,
            detailText]];

        NSString *pkgDir =
            [NSTemporaryDirectory() stringByAppendingPathComponent:@"MaxPS4PkgImport"];
        NSFileManager *fm = NSFileManager.defaultManager;
        [fm removeItemAtPath:pkgDir error:nil];
        NSError *mkdirError = nil;
        if (![fm createDirectoryAtPath:pkgDir
            withIntermediateDirectories:YES
                             attributes:nil
                                  error:&mkdirError]) {
            self.stateValueLabel.text = @"PKG reconnu";
            [self setDiagnostic:[NSString stringWithFormat:
                @"%@\n\nPKG reconnu, mais le dossier temporaire d’extraction n’a pas pu être créé : %@",
                detailText,
                mkdirError.localizedDescription ?: @"erreur inconnue"]];
            if (scoped) [url stopAccessingSecurityScopedResource];
            return;
        }

        NSString *plainExecutable =
            [pkgDir stringByAppendingPathComponent:@"eboot.bin"];
        BOOL extracted = maxps4_backend_extract_plain_pkg_executable(
            url.fileSystemRepresentation,
            plainExecutable.fileSystemRepresentation);

        const char *extractDetail = maxps4_backend_diagnostic();
        NSString *extractText = extractDetail
            ? [NSString stringWithUTF8String:extractDetail]
            : @"aucun détail d’extraction";

        if (!extracted) {
            self.stateValueLabel.text = @"PKG reconnu";
            self.guestOutputLabel.text = @"PKG inspecté • aucun exécutable direct lancé.";
            [self setDiagnostic:[NSString stringWithFormat:
                @"PKG reconnu : %@\nTaille : %llu octets\n\n%@\n\n"
                 "MaxPS4 n’utilise ici aucune clé et ne déchiffre pas le PFS ou le contenu protégé. "
                 "Un PKG standard/chiffré est donc reconnu et inspecté, mais pas extrait par ce chemin.",
                url.lastPathComponent,
                fileSize,
                extractText]];
            if (scoped) [url stopAccessingSecurityScopedResource];
            return;
        }

        // The readable homebrew executable is now in our own temp directory,
        // so the original security-scoped PKG can be released before FEX starts.
        if (scoped) {
            [url stopAccessingSecurityScopedResource];
            scopedForBoot = NO;
        }

        launchPath = plainExecutable;
        displayFile =
            [NSString stringWithFormat:@"%@ → eboot.bin", url.lastPathComponent];
        self.fileValueLabel.text =
            [NSString stringWithFormat:@"%@ • %llu o", displayFile, fileSize];
        detailText = extractText;
    }

    if (!maxps4_core_jit_available()) {
        const char *jitDetail = maxps4_core_jit_diagnostic();
        NSString *jitText = jitDetail
            ? [NSString stringWithUTF8String:jitDetail]
            : @"aucun diagnostic JIT";
        self.stateValueLabel.text = @"JIT indisponible";
        [self setDiagnostic:[NSString stringWithFormat:
            @"%@ validé, mais l’exécution FEX n’a pas été lancée.\n\n"
             "JIT requis : %@\n\n"
             "Active StikDebug/JIT puis réessaie. Cette vérification empêche FEX "
             "de démarrer sans mémoire exécutable valide.",
            displayFile,
            jitText]];
        if (scopedForBoot) [url stopAccessingSecurityScopedResource];
        return;
    }

    [self.guestUploadedTextures removeAllObjects];
    [self.guestUploadedTextureSequences removeAllObjects];

    self.bootInProgress = YES;
    self.bootGeneration += 1;
    const NSUInteger generation = self.bootGeneration;
    self.bootStartedAt = [NSDate date];

    self.importButton.enabled = NO;
    self.testButton.enabled = NO;
    self.multithreadTestButton.enabled = NO;
    self.stateValueLabel.text =
        importKind == MAXPS4_IMPORT_PKG ? @"PKG homebrew extrait" : @"Validation OK";
    self.guestOutputLabel.text = @"En attente de sortie…";

    [self setDiagnostic:[NSString stringWithFormat:
        @"Fichier sélectionné : %@\nTaille source : %llu octets\nValidation : %@\n\n"
         "Handoff loader → FEX en cours…",
        displayFile,
        fileSize,
        detailText]];

    [self startDiagnosticPollingForGeneration:generation];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL booted = maxps4_backend_boot(launchPath.fileSystemRepresentation);
        const char *bootDetail = maxps4_backend_diagnostic();
        NSString *bootText = bootDetail
            ? [NSString stringWithUTF8String:bootDetail]
            : @"aucun détail";
        if (scopedForBoot) [url stopAccessingSecurityScopedResource];

        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != self.bootGeneration) return;

            [self stopDiagnosticPolling];
            self.bootInProgress = NO;
            self.importButton.enabled = YES;
            self.testButton.enabled = YES;
            self.multithreadTestButton.enabled = YES;

            NSTimeInterval elapsed =
                self.bootStartedAt ? -[self.bootStartedAt timeIntervalSinceNow] : 0;

            char finalOutputBuf[4096] = {};
            maxps4_backend_live_output(finalOutputBuf, sizeof(finalOutputBuf));
            NSString *finalOutput =
                [NSString stringWithUTF8String:finalOutputBuf] ?: @"";
            self.guestOutputLabel.text =
                finalOutput.length ? finalOutput : @"Aucune sortie produite.";

            if (booted) {
                self.stateValueLabel.text = @"Terminé";
                [self setDiagnostic:[NSString stringWithFormat:
                    @"Exécution terminée : %@\nTaille source : %llu octets\n"
                     "Durée : %.2f s\n\n%@",
                    displayFile,
                    fileSize,
                    elapsed,
                    bootText]];
            } else {
                self.stateValueLabel.text = @"Handoff arrêté";
                [self setDiagnostic:[NSString stringWithFormat:
                    @"%@ validé.\nTaille source : %llu octets\nDurée : %.2f s\n\n"
                     "%@\n\nLe handoff loader → FEX a été tenté. Le diagnostic "
                     "ci-dessus indique précisément le dernier stade atteint ou le "
                     "service/HLE manquant.",
                    displayFile,
                    fileSize,
                    elapsed,
                    bootText]];
            }
        });
    });
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self stopDiagnosticPolling];
}
@end

@interface MaxPS4IntegratedAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation MaxPS4IntegratedAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[MaxPS4IntegratedViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([MaxPS4IntegratedAppDelegate class]));
    }
}
