/****************************************************************************************************
	EBNObservableInternal.h
	Observable
	
	Created by Chall Fry on 5/3/14.
    Copyright (c) 2013-2014 eBay Software Foundation.
	
	This header is intended for use only within EBNObservable and friends. 
	Client code shouldn't need it; this includes EBNObservable subclasses.
*/


#import "EBNObservable.h"

/**
	Used to track the shadow classes we create. Shadow classes are private subclasses of observed classes,
	and we isa swizzle the observed object to make it be one of these private subclasses. This dictionary
	maps base classes to EBNShadowClassInfo objects.
 */
extern NSMutableDictionary		*EBNBaseClassToShadowInfoTable;

/**
	Used as a private, global @synchronize token for EBNObservable. Your code should not sync against this.
	Currently points to EBN_ObserverBlocksToRunAfterThisEvent, but *could* point to any global object.
*/
extern NSMutableSet				*EBNObservableSynchronizationToken;

	// Used to disable this log warning when performance testing
extern BOOL ebn_WarnOnMultipleObservations;

/**
	The observedMethod dictionary has a NSMutableSet of these objects attached to each
	property being observed. This structure describes a single keypath that someone is
	observing. Each object in the observation path has this object in the dictionary for
	the property of that object being observed.
 */
@interface EBNKeypathEntryInfo : NSObject
{
@public
	NSArray		 			*_keyPath;
	EBNObservation			*_blockInfo;
}
@end

/**
	Observable keeps a static dictionary that maps Class objects to these info objects. There is one of these created
	for each shadowed class. This object is then responsible for tracking the overridden getters and setters that have
	been created for this class.
*/
@interface EBNShadowedClassInfo : NSObject
{
@public
	Class					_baseClass;
	Class					_shadowClass;
	bool					_isAppleKVOClass;
	NSMutableSet			*_getters;
	NSMutableSet 			*_setters;
}
- (instancetype) initWithBaseClass:(Class) baseClass shadowClass:(Class) newShadowClass;
@end

/**
	These are runtime-generated methods that we install on shadow classes with class_addMethod().
	Having these selectors in a protocol makes the compiler happy (well, happier, at lest).
 */
@protocol EBNObservable_Custom_Selectors

@optional
- (void) EBN_original_dealloc;
- (EBNShadowedClassInfo *) getShadowClassInfo_EBN;

@end


/**
	This is an category on NSObject whose definition and use are internal to Observable. It simply
	declares NSObject objects to conform to the EBNObservable_Internal protocol. Since the protocol
	definition itself is private to Observable, it's still private.
	
	These are all methods that the Observable code calls on itself to get things done, but which
	shouldn't be called from outside Observable. All these methods have the _ebn suffix to 
	reduce the chance they'll cause method namespace collisions.
*/
@interface NSObject (EBNObservable_Internal)

/**
	This is how Observable gets at the list of methods that are being observed.
	
	The returned dictionary is keyed on the properties currently being observed, and each key's value is a set
	of all the observations active on that property.

	@return A dictionary containing sub-dictionaries for each method which has an active observation.
 */
- (NSMutableDictionary *) observedMethodsDict_ebn;

	// When setting up an observation, or when an object in the middle of a keypath changes value, these
	// methods are used to set up observations on each object in the key path except for the endoint property.
	// That is, for an observation rooted on object A with the keypath "B.C.D", A will set up its local observation
	// on property B, and then call createKeypath: on object B. B will then do the same, calling object C.
- (bool) observe_ebn:(NSString *) keyPathString using:(EBNObservation *) blockInfo;
- (bool) createKeypath_ebn:(const EBNKeypathEntryInfo *) entryInfo atIndex:(NSInteger) index;
- (bool) removeKeypath_ebn:(const EBNKeypathEntryInfo *) entryInfo atIndex:(NSInteger) index;
- (bool) createKeypath_ebn:(const EBNKeypathEntryInfo *) entryInfo atIndex:(NSInteger) index
		forProperty:(NSString *) propName;
- (bool) removeKeypath_ebn:(const EBNKeypathEntryInfo *) entryInfo atIndex:(NSInteger) index
		forProperty:(NSString *) propName;

/**
	The Execute methods in EBNObservation can cause reaping, so Observable's reapBlocks is exposed 
	here for Observation's use.

	@return number of dead observations that got removed.
 */
- (int) reapBlocks_ebn;

	// Don't call these methods unless you have a good reason.
- (bool) swizzleImplementationForSetter_ebn:(NSString *) propName;
+ (SEL) selectorForPropertyGetter_ebn:(NSString *) propertyName;
- (Class) prepareToObserveProperty_ebn:(NSString *)propertyName isSetter:(bool) isSetter;

	// This is an optional method definied by LazyLoader but called by Observable. Downcasting at its finest.
- (void) markPropertyValid_ebn:(NSString *) property;

@end


// This describes a single observation block. There is one of these for each observationBlock,
// and much of the coalescing that takes place is actually unioning sets of these objects.
// Note that this object does *not* know the keypath(s) that it's observing.
@interface EBNObservation ()
{

// @public doesn't mean public to you--just to EBNObservable.
@public
	NSObject * __weak 		_weakObserved;
	
		// WeakObserver and its forComparisonOnly doppelganger should hold the same value; we have
		// both values so that we can compare an observation object's observer against the observer pointer
		// when the observer object is in the process of getting dealloced (generally, the observer's
		// dealloc method calls stopTellingAboutChanges: is how this happens). Zeroing weak refs will
		// return nil when the object pointed to is being deallocated, as they call objc_loadWeak().
		// The debugger, moreover, will show a non-nil value for the pointer to the being-dealloced object.
	id __weak 				_weakObserver;
	id __unsafe_unretained	_weakObserver_forComparisonOnly;
	
	
	ObservationBlock 		_copiedBlock;
	ObservationBlockImmed	_copiedImmedBlock;
}

@end

/****************************************************************************************************
	DEBUG_BREAKPOINT
	
	This is inline ASM code to programmatically break in the debugger at a specific point. Intended
	to be used with Apple's AmIBeingDebugged() method. Works on ARM and x86 processors, and their 64
	bit variants.
	
	DO NOT use this macro in your code to try debugging something. You'll forget, leave it there,
	and then your code will ship with a debugger break in it and will crash for no reason for your users.
	
	At some point, years from now, someone is going to have to have to extend this to a new target CPU type.
	Sorry.
*/
#if TARGET_CPU_ARM
	#define DEBUG_BREAKPOINT \
	({ \
		__asm__ __volatile__ ( \
				"mov r0, %0\n" \
				"mov r1, %1\n" \
				"mov r12, #37\n" \
				"svc 128\n" \
				: : "r" (getpid ()), "r" (SIGINT) : "r12", "r0", "r1", "cc"); \
	})
#elif TARGET_CPU_ARM64
	#define DEBUG_BREAKPOINT \
	({ \
		__asm__ __volatile__ ( \
				"mov x0, %0\n" \
				"mov x1, %1\n" \
				"mov x12, #37\n" \
				"svc 128\n" \
				: : "r" ((long) getpid ()), "r" ((long) SIGINT) : "x12", "x0", "x1", "cc"); \
	})
#elif TARGET_CPU_X86
	#define DEBUG_BREAKPOINT \
	({ \
		__asm__ __volatile__ ( \
				"pushl %0\n" \
				"pushl %1\n" \
				"push $0\n" \
				"movl %2, %%eax\n" \
				"int $0x80\n" \
				"add $12, %%esp" \
				: : "g" (SIGINT), "g" (getpid ()), "n" (37) : "eax", "cc"); \
	})
#elif TARGET_CPU_X86_64
	#define DEBUG_BREAKPOINT \
	({ \
		__asm__ __volatile__ ( \
				"int $3" \
				: : : "cc"); \
	})
#else
	// Can't break. Unknown cpu target.
	#define DEBUG_BREAKPOINT
#endif


