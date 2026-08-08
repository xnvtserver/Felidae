@mixfix(
    pattern: "about {left:expr}"
)
about() =>
     system.print(value:"Felidae is a family of mammals in the order Carnivora, commonly known as cats. It includes domestic cats, lions, tigers, leopards, and other wild cats.")
    system.print(value:"Felidae are characterized by their retractable claws, sharp teeth, and keen senses, which make them effective hunters. They are found in various habitats around the world, from forests and grasslands to deserts and mountains.")
 return 

 @mixfix(
    pattern: "about felidae features"
)
aboutFeatures() =>
    system.print(value:"Felidae features include:")
    system.print(value:"1. Retractable claws: Most felids have retractable claws that they can extend when hunting or climbing.")
    system.print(value:"2. Sharp teeth: Felids have sharp canine teeth for capturing and killing prey.")
    system.print(value:"3. Keen senses: They have excellent vision, hearing, and sense of smell, which aid in hunting.")
    system.print(value:"4. Solitary behavior: Many felids are solitary animals, although some species may form social groups.")
    system.print(value:"5. Carnivorous diet: Felids are obligate carnivores, relying on meat as their primary food source.")

 @mixfix(
    pattern: "{left:expr} is {right:expr} "
 )
 is()=>
   if left.__sign__ == "yes {input:str}" then
     if input == "feliade" then
     return about()
   else
    return "unknown operation"


feliade()=>
 # about felidae
 user_input := system.input(prompt: "Would you like to learn more about Felidae?")
  user_output: system.run(transform: user_input)
 system.print(value:user_output)

